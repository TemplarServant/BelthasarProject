# THE WRITE CENSUS OF A LOCATION LOAD — the clear list, measured

Closes the four items `wram_lifetime.md` §7 lists as **blocking the WRAM ownership move**. That file
established the *direction* (the ROM's `$7E` is process-lifetime; the port's is scene-lifetime) and
then stopped, because §6 says plainly: move `$7E` to the host and you must supply an explicit clear
list, and **any clear list written down today is INVENTED**.

It is no longer invented. It is measured, and it is four DMA transfers.

The reason every previous attempt failed is a single fact: **not one of the location load's clears is
an instruction.** All four go through `C0/2DF1`, which points DMA channel 7 at `$2134` (the PPU
multiplier's low byte, forced to 0) and lets the transfer write the zeros. A `STA`/`STZ` xref scan
over the whole ROM cannot find them, `--monitor --writer` reports no writer for the bytes they touch,
and a sentinel diff sees the effect with no cause. §7 item 1 called this "the single biggest unknown";
it was unknowable with the instruments that existed.

---

## 0. THE INSTRUMENT — `ct.exe --loadcensus`

Added for this pass (`src/main.c`, `runLoadCensus`). It logs **every CPU write** across a location
transition and reconciles the log against a before/after image of all 128 KB, producing three classes:

| class | meaning |
|---|---|
| **CPU** | changed AND written by a logged instruction — the writer PC is named |
| **DMA?** | changed with **no** logged CPU write — `snes/dma.c:350/353` calls `snes_write`/`snes_writeBBus` directly, never `cpu->write`, so DMA is structurally invisible to the hook. **A reported blind spot, and the whole point of reporting it.** |
| **IDEM** | written but the value did not change — **invisible to any before/after diff**, and still a byte the load OWNS |

`IDEM` is the class this pass exists for. On a field→field load it is **58293 bytes** against 6386
changed: the load writes an order of magnitude more memory than it visibly alters. For the ownership
move that distinction is the whole question — a byte the load rewrites may hold anything beforehand, a
byte it never touches must SURVIVE — and no sentinel diff can separate them.

Two resolution details the mode has to get right, both of which bit during this pass:

- **The `$2180` WRAM port.** A store to WMDATA lands at the `$2181-83` latch address, so the log
  records a write to `$2180`. `CtWrite` now carries `ramAdr`, captured live at hook time (a replay
  from the log alone would desync, because DMA also advances the port and is invisible). 27314 of the
  writes in a field load resolve this way — a census that ignored the port would lose all of them.
- **Strictness.** `ctWramOff()` maps *any* bare address below `0x20000` to a WRAM offset, which is
  right for a `$7E` store but bills a bank-`$00` PPU write like `$002100` to WRAM offset `$2100` —
  inventing writers for the `$21xx` page, one of the very spans §7 lists as unexplained.
  `ctCensusOff()` is strict: `$7E`/`$7F` direct, the `$0000-1FFF` low-RAM mirror, `$2180` via the
  latch, everything else **not WRAM**.

```
# the natural door, with its control
ct --loadcensus saves/truce_market.state --btnscript "0:10,20:60,0:200" \
   --censuspre 40 --censuspost 60 --censusout door.map
ct --loadcensus saves/truce_market.state --btnscript "0:10,80:45,40:45,0:100" \
   --censusnull --censuspre 40 --censuspost 60 --censusout ctrl.map

# a field->field load (SYNTHETIC entry) + the ordered history of one address
ct --loadcensus fresh439.state --censustele 2 8 8 1 --censuspost 60 --censusat 7F0200
```

`--censusout` writes 3 × 128 KB (per-byte flags, before image, after image) so a run can be subtracted
from a control offline.

---

## 1. ⭐ THE CLEAR LIST — four DMA transfers, and nothing else

Identical on both source states tested (`fresh439.state` loc 439, `saves/truce_market.state` loc 17),
both loading loc 002. **The list is source-independent**, which is what makes it a clear list and not
an observation.

| # | setup | call | window | bytes |
|---|---|---|---|---|
| 1 | `C0/7F87` | `C0/7F95` | **`$7E:1D00-1DFF`** | 256 |
| 2 | `C0/5709` | `C0/5717` | **`$7F:0200-03FF`** | 512 |
| 3 | `C0/58AB` | `C0/58B9` | **`$7E:1900-19FF`** | 256 |
| 4 | `C0/58BC` | `C0/58CA` | **`$7F:0580-1B3F`** | 5568 |

Plus, per object, in the init runner: `58ED STZ $1B01,X` and `58F0 STZ $1B80,X` (these ARE
instructions and the census names them).

The three setups are the same five stores each — length into `$4E`, address into `$4B`, bank into
`$4D` — for example:

```
58AB: LDX #0100 / STX $4E        ; DAS7  = 256
58B0: LDX #1900 / STX $4B        ; A1T7  = $1900
58B5: LDA #00   / STA $4D        ; A1B7  = $00   <- the LOW-RAM MIRROR of $7E:1900
58B9: JSR C0/2DF1
58BC: LDX #15C0 / STX $4E        ; DAS7  = 5568
58C1: LDX #0580 / STX $4B        ; A1T7  = $0580
58C6: LDA #7F   / STA $4D        ; A1B7  = $7F
58CA: JSR C0/2DF1
```

⚠ **Two of the four name their destination through the bank-`$00` low-RAM mirror, not `$7E`.** My own
DMA reporter first required `A1B7 ∈ {$7E,$7F}` and silently missed both of them; the only reason that
did not become a confident wrong answer is that the `DMA?` bucket still counted their changed bytes
and the numbers did not add up. A clear list derived by grepping for `A9 7F 85 4D` would be **half the
real list**.

### 1.1 The primitive — `C0/2DF1`, a hardware zero source

```
2DF1: LDA #00
2DF3: STA $211B / 2DF6: STA $211B      ; M7A = 0000  (two writes fill the 16-bit latch)
2DF9: STA $211C / 2DFC: STA $211C      ; M7B = 0000
                                       ; => $2134 (MPYL) reads 00 for as long as this holds
2DFF: LDA #80  / 2E01: STA $4370       ; DMAP7 = $80 = B->A, FIXED B address, 1-byte unit
2E04: LDA #34  / 2E06: STA $4371       ; BBAD7 = $34 -> $2134
2E09: LDX $4B  / 2E0B: STX $4372       ; A1T7  = destination address
2E0E: LDA $4D  / 2E10: STA $4374       ; A1B7  = destination bank
2E13: LDX $4E  / 2E15: STX $4375       ; DAS7  = length
2E18: LDA #80  / 2E1A: STA $420B       ; fire channel 7
2E1D: RTS
```

`B->A` is the direction that matters: the transfer's **destination is the A-bus address itself**, so
this is not a "DMA into the WRAM port" at all and a census looking only for `BBAD == $80` sees
nothing. Callers, via `ct.py refs 2DF1`: `C0/0031`, `C0/0042`, `C0/0050` (the three cold-init wipes
inside `C0/000E`, which is what `wram_lifetime.md` §1 was counting), `C0/5717`, `C0/58B9`, `C0/58CA`
(the location load), `C0/7F95`, `C9/FB1F`.

### 1.2 The overworld leg clears NOTHING

The natural field→overworld door fires **506 DMA transfers and 0 of them into WRAM**, and its `DMA?`
bucket is **empty**: every changed WRAM byte has a named CPU writer. Consistent with `C0/0066`'s
`CPX #01F0 / BMI / JML C2/0000` — the overworld leg never reaches `C0/5709`, so it never reaches the
clears. Whatever the port does for `$7E` ownership must therefore **not** clear on a world entry.

---

## 2. §7 ITEM BY ITEM

### Item 1 — "which routine clears the per-object tables" → §1 above. **CLOSED.**

`C0/58EA-5922` is the init runner (already corrected in `wram_lifetime.md` §6) and it clears
`$1B01,X`/`$1B80,X` per object; the *page-sized* clears are the four DMAs. The census names
`C0/58F0` as the writer of `$7E:1B01` — see the PC convention in §4.

### Item 2 — "why the `$0F00` overwrite spans only 63 bytes" → **THE PREMISE DOES NOT REPRODUCE.**

The load's **write set** in that page is `$0F00-$0F27` (40 B) + `$0F80-$0FA7` (40 B), *identical on
both source states*. Neither is 63 bytes and neither ends at `$0F3E`. The *changed* subset is
scattered and data-dependent (on 439→2 it ends at `$0FA6`). The dossier's "63 bytes (`$0F00-$0F3E`)"
was a changed-byte count read as a write extent; those are different measurements and only the second
is a property of the ROM.

What produces each block, both confirmed from the writer side:

```
C0/A7E9   A7F2: LDX #0F00 / STX $81      ; port = $7E:0F00
          A7FB: SEP #10                  ; ★ X/Y become EIGHT-BIT
          A801: LDX #00 / A803: LDY #FF
          A805: STX $80 / A807: STY $80  ; one byte each -> the port MARCHES
          A809: DEC A / BNE ->A805       ;   => $0F00-$0F27 = 00 FF x20

C0/585C   585E: LDX #0F80 / STX $81
          5863: SEP #10                  ; ★ same SEP
          5869: LDX #80 / 586B: LDY #00
          586D: STX $80 / 586F: STY $80  ;   => $0F80-$0FA7 = 80 00 x20

C0/B246   B246: LDX #0F01 / STX $81
          (NO SEP #10 -- X/Y stay SIXTEEN-BIT)
          B24F: LDX #00FF / B252: LDY #0000
          B259: STX $80                  ; low byte -> WMDATA, HIGH byte -> WMADDL = 00
          B25B: STY $80                  ; same, and WMADDL = 00 again
          B25D: DEC A / BNE ->B259       ;   => hammers $0F00 forever, alternating FF/00
```

⚠ **`wram_lifetime.md` §4a is CONFIRMED, and the cause is now one token.** The three loops are the
same shape; `C0/B246` differs from the other two **only by the missing `SEP #10`**, which is what turns
each store into a 16-bit store that clobbers WMADDL and pins the port. §4a proved this with a sentinel
(`$0F29` still `0xA5`); the census proves it from the writer side — the ordered history of `$7E:0F00`
is literally `00 by C0/B25D, FF by C0/B25B, 00, FF, …`, one pair per iteration, and `$0F02` upward
never appears. Do not "fix" that loop's port setup: it is the ROM.

### Item 3 — "what writes `$7E:117E/117F`" → **NOTHING DOES.** **CLOSED, as a negative.**

The load's write runs in that page are `$1100-$117D` and `$1180-$11A7`: a real two-byte hole, exactly
where `C0/57A0`'s 63-entry fill stops and `C0/5731`'s pointer fill starts. `$7E:117E` has no logged
write, no near miss under any bank, and does not change (`before=00 after=00`). §6's "the `$7E:1100`
overwrite runs contiguously to `$11A7`" is refuted — it was a changed-byte observation, and the two
bytes happened to differ for another reason on that transition.

A negative is the fragile kind of verdict (memory: `verify-negatives-on-proper-state`), so
`--censusat` reports **near misses**: any logged write whose low 16 bits match, with the bank it
actually carried. If the resolver had dropped the write, the negative would be mine and it would say
so. It reports none.

### Item 4 — "does the ROM clear `$7F:0200-03FF`" → **YES. `events.md:40` IS CORRECT.** **CLOSED.**

Clear #2: 512 bytes at `$7F:0200`, set up at `C0/5709`, fired at `C0/5717`. It was never found because
it is not an instruction.

⚠ Note *why* the census settles this and a sentinel cannot: **a clear is a write regardless of the
value it writes.** A 512-byte zero-fill over an already-zero page produces 512 writes and zero changed
bytes. §7 correctly called the old evidence "a weak 0→x test" — the CPU-side half of this census is
equally weak on that page (2 CPU writes, `$7F:020C` and `$7F:0214`), and it is only the DMA leg that
answers it. Had the DMA scan not been written, this pass would have concluded "NO, only 2 bytes" with
exactly the same confidence.

---

## 3. THE COVERAGE GAP — one natural door captured

§7: "**No natural door transition was ever captured.**" One now is:

```
ct --loadcensus saves/truce_market.state --btnscript "0:10,20:60,0:200"
   -> loc 011 -> 1F0, transition at f43, walked through the real door
```

Truce Market (scene 17) has exactly one exit — `maps.scene_exits` gives pos (336,736) size 32×16,
kind `world` — and the lead PC sits at (352,719) in `truce_market.state`, 17 px north of it, so a
plain Down walks out. This is a **field→overworld** door. Field→field and overworld→field are still
measured with `--censustele`, which the mode labels `SYNTHETIC entry at C0/0066` in its own banner.

Attempts that did NOT work, recorded so they are not retried: walking back in from the overworld
(the player moves — `$0D84`/`$0D88` respond from ~f190 — but never re-enters); and
`--teleport 1 9 7 2` into Crono's house, where all four control gates read open
(`$0162=00 $011F=01 $0138=00 $0120=01`) and the lead still will not move in any direction.

---

## 4. GOTCHAS

- ⭐ **The reported write PC is the address of the NEXT instruction.** The CPU has advanced past the
  opcode when the bus write fires. `58ED STZ $1B01,X` reports `C0/58F0`; `591B STA $1180,X` reports
  `C0/591E`; `A805 STX $80` reports `C0/A809`. Already documented for `--lifetrace`; the census
  inherits it. Every writer PC in this file follows that convention.
- ⭐ **`--censustele`'s own poke shows up as `DMA?`.** The harness writes `$0100-$0104` straight into
  `s->ram`, which is not a CPU write, so 5 of the teleport run's `DMA?` bytes are *my hand*, not the
  ROM's. The natural door has no poke and reports `DMA? = 0`. Read the bucket, do not just count it.
- ⭐ **`dis.py` desyncs on `A2 00 02` at default widths** — read as x=1 it is `LDX #$00` plus a stray
  `02` (`COP`), and every byte after is garbage. `C0/5709`'s clear setup decoded as `COP #86 / LSR
  $00A2` until re-read with `x=0`. Third time this exact trap has cost time; pass `m x` always.
- The log holds 1M entries and the mode **says so when it saturates** rather than truncating quietly.
  A 240-frame window on a field load logs ~611 k writes; a wider one will not fit.
- Choose the window by measurement, not by taste: `written` plateaus (66073 → 66236 between post=60
  and post=120, +163 over 60 frames) which is where the load ends and ordinary churn begins.
  `changed` is **not** monotonic in the window — it dips at post=30 because a block is cleared and
  later repopulated — so sizing a window by the changed count reads the load backwards.

---

## 5. THE INSTRUMENT IS VALIDATED AGAINST INDEPENDENTLY DERIVED FACTS

`wram_lifetime.md` §1's fill table was built by hand from disassembly plus sentinel diffs — a
different method entirely. Every entry reproduces, writer PCs included (+1 per §4):

| §1 claim | §1's ROM | census writer | value |
|---|---|---|---|
| `$7E:0B80-0B87` = 0x80 | `C0/7155` | `C0/715A` | 80 ✓ |
| `$7E:0B88-0BA5` = 0x80 | `C0/6F79` | `C0/6F93` (via the port) | 80 ✓ |
| `$7E:0BC0-0BC7` = 0x80 | `C0/E935` | `C0/E942` | 80 ✓ |
| `$7E:0920-092F` = 0xFF | `C0/5929` | `C0/592E` | FF ✓ |
| `$7F:1CC8-1CE7` = 0x8080 | `C0/28AA` | `C0/28B6` | 80 ✓ |

And §1's one **PROVEN** claim holds in the stronger form: `$7F:0000-01FF` takes **zero writes**, not
merely "survives" — a clear would have shown up as writes even had the values matched.

New, not in §1: `$7E:0920` is written `FF` by `C0/592E` and then **`24` by `C0/5CBB`** — the fill has a
second writer.

---

## 6. ⭐ BONUS — `wram_lifetime.md` §2's sub-frame question, answered

§2 could not exclude that `$7E:1E00-1EFF` is wiped and exactly restored *within* one frame, because
`--monitor --writer` keeps only the last writer per region per frame: "closing it needs a per-CPU-step
write watch over the full page." That is what this mode is.

| run | written | changed | IDEM | mailbox `$1E00-03` |
|---|---|---|---|---|
| natural door 011→1F0 | **5** | 0 | 5 | `00 1E E8 0D` → unchanged |
| 011→002 | **5** | 0 | 5 | `00 1E E8 0D` → unchanged |
| 439→002 | 119 | 66 | 53 | `00 06 C8 05` → `10 1E E8 0D` |

**Five writes is not a wipe** — a wipe-and-restore of the page would be ≥256 writes in the log
regardless of the values, which is exactly the visibility a diff lacks and this has. §2's residual
doubt is closed: the mailbox page is not wiped by a location load.

The 439→2 row is not a counterexample: that transition genuinely *changes the song* (loc 439 → loc 2,
whose `$F6` record declines, so the script's op `$EA` stages a different one), and the writers are the
audio path. Compare `ct-audio-system` — `$29AE` is the shadow, not the resident song.

## 7. THE CHECKS, AND THE MUTATIONS THAT KILL THEM

Pinned as `wram-loadcensus-clearlist` and `wram-loadcensus-natural-door` (both `quick`). A green check
proves nothing until a mutation kills it, so both were mutation-tested against the instrument:

| mutation | clearlist | natural door |
|---|---|---|
| A-bus WRAM test loses the bank-`$00` low-RAM mirror (**the bug I actually shipped first**) | **FAIL** — 2 of 4 clears vanish | pass (it has no WRAM DMA to lose) |
| `ctCensusOff` stops resolving the `$2180` port | **FAIL** | **FAIL** |
| no-op control (restored source, rebuilt) | pass | pass |

⚠ The harness restores the source but **not** the binary — rebuild before running anything else, or a
later `--pin` records the mutated score (memory: `ct-vm-opcode-coverage`).

## 8. ⭐ THE OWNERSHIP MOVE LANDED — and what it cost

`$7E`/`$7F` now belong to the HOST for the whole run (`CtWramCreate` / `CtWramLocationLoad` /
`CtSceneCreateIn`, `field.c`'s `g_wram`). A location load applies the four clears above and nothing
else; everything else carries over, as in the ROM. `CtSceneCreate()` keeps the old self-contained
behaviour for the harness modes that build one throwaway scene.

**The NULL guard is gone.** `field.c` serviced the sound mailbox as `if (f.vm) CtAudioService(...)`
because `$1E00` lived in the scene VM's WRAM and the overworld has no scene — the guard suppressed a
crash without restoring the mailbox. It is now `CtAudioService(audio, CtWramSys(fieldWram()))`,
unconditional, because the page is simply always there. (Still not ported, unchanged by this: the
overworld's own song change, `C2/2F25` → `$02AE` and `C2/3009`'s region compare.)

### ⚠ THE COUPLING THIS EXPOSED — `C0/0B86-0C75` was never ported

Flipping persistence on turned two checks red (`field-newgame-pan`, `field-scene120-lucca-e0-fadechange`),
and the cause was not the ownership move. **The port had never modelled the load's constant-init pass
over the engine's direct page**; it got the same effect for free from zeroing the image on every scene
create. `sc->locChange`'s own comment had named exactly one line of it — *"the new scene's memInit
models the load's `STZ $17` (C0/0B96)"* — and there are about seventy. With the memset gone, the
op-0xE0 pending-change latch `$0117` survived into the next scene and re-fired the change immediately;
the scene-120 run ended up back in scene 0.

`C0/0B86-0C75` is now ported verbatim (`memLoadInit0B86`). It was read with `dis.py 0B86 <len> 1 0`
and then **confirmed against the oracle**, 19 of 19 spot checks matching value *and* writer PC:

| addr | value | census writer | store |
|---|---|---|---|
| `$010A` | 02 | `C0/0B8A` | `0B88 STX $0A` |
| `$0121` | E0 | `C0/0BA2` | `0BA0 STA $21` |
| `$0BDE` | 02 | `C0/0BAB` | `0BA8 STA $0BDE` |
| `$011F` | 01 | `C0/0BC8` | `0BC6 STA $1F` |
| `$0128` | 5F | `C0/0BD2` | `0BD0 STA $28` |
| `$01AF` | 00 | `C0/0BFE` | `0BFC STA $AF` (16-bit, from `$E4:FFE0`) |
| `$2000` | 00 | `C0/0C1A` | `0C16 STA $7E2000` |
| `$0197` | 80 → 02 | `C0/0C22`, then `C0/42B5` | `0C20 STA $97`, then the loader's real lead |
| `$01F8` | 1D | `C0/0C75` | `0C73 STA $F8` (from `$7E:0400`) |

…and ten more, all matching. **A linear disassembler desyncs; the measurement is what licenses the
transcription.** Notes that bit or nearly bit:

- `0BF4 REP #20` makes everything to `0C1C` **sixteen-bit**, so each `STZ` there clears a PAIR
  (`$012B/$012C`, `$0150/$0151`, `$0158/$0159`).
- `0C20 STA $97` sets `$0197 = 0x80` — the LEAD-PC offset — and the object loader fixes it to the real
  value at `C0/42B5`. Reading only the first write would look like a port bug.
- `0C34-0C44` **reads** `$7E:2980-82` into `$0194-96`. The party is read and never written, which is
  the same fact `wram_lifetime.md` §4 measured with sentinels — and the reason the port's
  `g_carryParty` existed at all.
- `$E4:FFE0/E2/E4/E6` = `2000 2300 2600 2800`, baked (the VM is ROM-free at runtime) beside the `$E4`
  bank bytes from `0BBA`.

### ✅ The three carry statics are RETIRED

`g_carryParty` (`$7E:2980-82`), `g_carryConfig2990` (`$7E:2990`) and `g_sheetCarry` (`$7E:2600`,
0x280 B) existed only to simulate persistence the image now provides — `g_sheetCarry`'s own comment
said so: *"our engine loads each scene into a fresh $7E"*. All three are gone, along with
`CtSceneSetCarryParty` / `CtSceneSetCarryConfig` and the capture blocks at both transition sites.
`g_carrySong` STAYS: it models the ROM's *order* (`C0/1B53` before `C0/0960`), not persistence.

Two things the removal needed, neither of them a deletion:

- **The new-game party default moved to `memBoot`.** `CtSceneCreateIn` used to write Crono-only +
  six empty reserve slots on every scene create and then let the carry overwrite it. With the carry
  gone that write would erase a follower on every door, so the *default* now belongs to cold init —
  which is also where the ROM keeps it: the load **reads** `$2980-82` (`0C34 LDA $7E2980 / 0C38 STA
  $94`) and never writes them. The create-time `w8()` writes survive **gated on `sc->ownMem`**, so the
  private-image path still claims those addresses in the `--vmdiff` write mask.
- **`CT_CARRYPARTY` is kept but re-aimed.** It was never persistence — it is a test hook — so it now
  writes the party straight into the host image.

⚠ **NO PRE-EXISTING CHECK CROSSED A DOOR WITH A FOLLOWER IN THE PARTY**, so nothing in the suite
would have caught the follower being erased. `--wramlife` grew a second line for exactly this:

```
wramlife: carries -> party=00,01,80 config2990=40 sheet=ALIVE
```

with `CT_CARRYPARTY=0,1,128` forcing Marle into slot 1 before the first load, measured after the
house → overworld → market → overworld round trip. The default run reads `00,80,80`, so the two are
distinguishable — a per-load re-seed would read `00,80,80` even with the env var set. `$2990` and the
sheet carry a stamped marker instead (`$2990 = 0x40`, the ATB bit `C2/E24C`'s `TSB` sets; `$287F`,
char 7's last sheet byte, inert for a 1-2 char party). **MUTATION:** un-gating the party seed
(dropping `if (sc->ownMem)`) reads `party=00,80,80` — the follower erased on every door, which is the
bug `g_carryParty` was written to paper over in the first place.

⚠ Found while wiring that probe: the ownership commit had left **two** `CtWramLocationLoad()` calls in
`fieldLoad` — the clears ran twice per load. Idempotent, so nothing observed it; fixed here.

## 9. ⭐ THE WORLD RUNS ON THE HOST IMAGE TOO

`CtWram` is now laid out as a **flat 128 KB** — `$7E` at `[0, 0x10000)`, bank `$7F` at
`[0x10000, 0x20000)` — which is exactly the linear index `wmIndex()` already handed out, and exactly the
view `world.c` wants for its `ram + 0x10000 + pc` script reads. So `worldInit` stopped calling
`calloc(1, 0x20000)` and simply uses the host image. The hand-written bridge that carried `$29AE`,
`$02AE`, `$7F01ED` and the mailbox between two images is **gone**; those bytes are CPU-global in the
ROM and they are single-copy here again.

The field order of `CtWram` is therefore load-bearing (`s` is the base of the *whole* image, `g`/`l` are
views into bank `$7F` at their real offsets) and a `ct_wram_layout_assert` proves the two offsets at
compile time.

### ⚠ IT BROKE 69 CHECKS, AND THE CAUSE WAS AN EAGER LOAD

`field.c` called `worldInit(&assets, &wd, 0)` **at startup**, before any scene existed. That was
harmless while the world had a private image and catastrophic the moment it shared the host's: a world
load writes `$7E:3000-7E00` (four map grids), the object array `$0B30-1B30`, and the entry records at
`$7E:7E00 + nrec*7`. Loading world 0 up front therefore wiped `$7E:5800-7000` before the battle ever
read it, and **every battle check reported a blanked enemy table** (`id $612D = FF`, `maxHP = 1`).

The fix is not a guard: **the ROM only puts world data in `$7E` on a world ENTRY**, so the port must
too. The startup call is gone — availability is a question about the ASSET, so it asks the asset
(`CtAssetGet(CT_ASSET_WORLD, 0)`) — and the two env-gated probes that genuinely need a loaded world
(`CT_OWBOAT_SHOT`, `CT_OWRIDE_TEST`) load it themselves.

That the field and the battle share `$7E:3000-7E00` **with the world** is not a bug to design around;
it is what the ROM does, and the private image had been hiding it.

### The `--wramlife` witnesses had to be rechosen

Same cause, and it is worth stating rather than quietly re-pinning: `$1A00` sits inside the world's
object array and `$8000` inside its entry records, so both died **for a perfectly correct reason** once
the world shared the image. The alive witnesses now all sit in `[$1BAB, $3000)` — the band between the
`$1B9B` song table and the map grids that a world load does not touch — and the `$1D00` window is
witnessed from **both** edges (`$1CFF` alive below, `$1DFF` dead at its last byte, so a clear that runs
long and one that stops short each show up). `$1940` stays but is now **over-determined** (the object
array covers it too), so its death no longer proves the `$1900` clear;
`wram-loadcensus-clearlist` does that by seeing the DMA itself.

### ⚠ A DEFECT THE OWNERSHIP COMMIT INTRODUCED, found here — now PINNED

`b970585` turned `Mem mem` inside `CtScene` into `Mem* mem`, and the debug savestate broke **silently**
in two ways at once: the pointer was neither scrubbed on save (a heap address on disk — the exact thing
the format's v2 bump was for) nor preserved on load (`memcpy`'d a stale pointer over the live one), and
**the 128 KB it used to carry stopped being saved at all**, so a reload restored the VM on top of
whatever memory happened to be there. Format v3 writes the image explicitly, reads it into a temp first
so a truncated tail cannot half-write live WRAM, and copies it back **into** the existing buffer rather
than repointing — the overworld's `w->wram` and the audio service both hold that pointer.

Pinned as `state-roundtrip-image`: `CT_STATEROUNDTRIP=<frame>` does save → **poison** → load → compare,
all in ONE process, so there is no cross-check ordering to get wrong under `--jobs`.

```
stateroundtrip: f40 save=ok poisoned=yes load=2 image=MATCH player=(104,112)
```

⚠ **`poisoned=yes` is the denominator and it is in the pin.** Without it `image=MATCH` is vacuous — a
save/load pair that does *nothing whatsoever* also leaves the image untouched and reads MATCH. The
poison XORs `0x5A` every 977 bytes (a coprime stride, so every page is hit) and the report asserts that
this really moved the fnv, which is what makes MATCH mean *the load undid it*. Mutations, all three
killed: dropping the image write (the original defect) → `load=0 image=DIFFER`; not restoring the image
→ `image=DIFFER`; not preserving `sc->mem` → `image=DIFFER`, because the restore then writes through the
stale pointer and the live poison survives (that mutation is UB and merely happened not to crash).

## 9b. CLEAR #4 IS PERFORMED NOW, AND THE FIRST BOX OF A SCENE DEPENDED ON IT (2026-09-06)

`memLocationLoad` recorded clear #4 (`C0/58CA`, `$7F:0580-1B3F`) with the note "the port models $7F only
up to $0400 ... if $7F above $0400 is ever modelled, this clear belongs with it". It has been modelled
since `Mem::hi` was added for the drain's sprite templates, so the clear is now performed:
`memset(m->hi + (0x0580 - 0x0400), 0, 0x1B40 - 0x0580)`.

Some of the 5568 bytes DO have labels, and one of them is load-bearing: **`$7F:0A00,X` is op 0xBB's
per-object latch** (`C0/3580` reads it, `C0/3588` sets it on the REQUEST arm, `C0/3635` clears it on the
COMPLETION arm). A latch left non-zero across a location load makes that object's next textbox op take
the completion arm -- it clears the latch, advances past the op and posts NO box. Measured on a `--newgame`
run to the bedroom: Mom (obj8) executed her `BB 06` at body `$030A` with `$7F:0A10 = $0B`, the box never
reached the machine, and the first box the player saw was her NEXT line (`BB 07`). The user reported it as
"example of a text that does not occur" and captured it (`texto_que_nao_ocorre.state`, loc 2, storyline 0,
spliced to `saves/field_re/txtmiss_lake.state`): the oracle has obj8 parked on `$058A` = `BB 06` with
"MOM: Come on, sleepy head! / Get up!" (table `$F7:0000`, string 6) on screen.

Other labelled tenants of the span, all of which the ROM therefore resets per load: the op-0xAD wait
counter `$7F:0A01,X` (C0/2F96), the CALL wait flag `$7F:0980,X`, the op-result page `$7F:0A80,X`, the
`$7F:0B00,X` move lifecycle (C0/AA07) and the party VELOCITY RING `$7F:0C00-0F7F` -- which is consistent
with the PC loader clearing the ring cursors `$01AB/$01AC/$01AD` on the same load (party_follower_ring.md
section 9). Check: `field-newgame-mom-wakeup-text`, mutation = drop the memset. Probe: `CT_TBOPDBG=1`.

## 10. STILL OPEN

- **Most of the `$7F:0580-1B3F` clear is still unlabelled** (§7's unlabeled list includes
  `$7F:0C14-1532`, which sits inside it). The tenants named in §9b are derived; the rest is not.
- **Which of the four clears the OVERWORLD leg would need, if any** — measured only as "none fire".
  Battle entry/exit, the Epoch/time-gate world change and menu-driven warps are still unsampled
  (§7's list, unchanged).
- `$7E:1D00-1DFF` (clear #1) is fired from `C0/7F95`, whose **caller** was not chased.
- **The census is CPU-write + DMA-setup, not a true bus watch.** The DMA leg reconstructs transfers
  from the logged `$43x0-6`/`$420B`/`$420C` stores; a transfer whose registers were set up *before*
  the logging window opens would be decoded with stale parameters. Both windows here open ≥40 frames
  ahead of the load and all four clears set every register immediately before firing, so it does not
  bite — but it is the assumption to break first if a future run disagrees.

Related: `wram_lifetime.md` (this file closes its §7 1-4), `events.md`, `object_loader.md`.

---

## ADDENDUM 2026-09-07 — the clear list was four DMAs. The LOAD is not.

`§1` is still right about what it measured: the page-sized clears of a location load are four DMA
transfers and nothing else. What it did not measure -- because the mode's whole subject was those
clears -- is the **per-object DEFAULT FILL** that runs between two of them, `C0/571A-58AA`.

It is invisible three ways over. It is not a DMA, so the DMA reporter does not see it. It is not a
`STA $1000,X`, so a grep does not see it (bank `$C0` holds exactly six of those and every one
preserves bit 7). And it is not a before/after change on most bytes, because the values it writes are
usually the values already there. Every store goes through the **PPU's WRAM port**: `571A PHD /
572B LDA #2100 / 572E TCD`, so `$81/$82/$83` are WMADDL/WMADDM/WMADDH and `$80` is WMDATA, and each
array is `LDX #<even> / LDY #<odd> / STX $80 / STY $80 / DEC A / BNE`.

Reached by `--censusat`, which reports the writer PC and therefore does see it:

```
build/ct.exe --loadcensus saves/ow_w0.state --censustele 6 9 13 0 --censusat 7E1010
  loadcensus: write history of 7E:1010:
      [ 0] seq=381208  = 04  by C0/5781  (via the $2180 WRAM port)
```

### The fill, in the ROM's order

| ROM | array | per object |
|---|---|---|
| 5731-5758 | `$1180,X` | the object's **fn0 pointer**, read from the script's own 32-byte-per-object table at `$7F2001`; ends on `CPX $01D9` with `$01D9 = nobj<<5` |
| 575A-576E | `$1C00,X` / `$1C01,X` | **07** / 00 |
| 5772-5786 | `$1000,X` / `$1001,X` | **04** / 00 |
| 578A-579C | `$1080,X` / `$1081,X` | **80** / **80** |
| 57A0-57B2 | `$1100,X` / `$1101,X` | 80 / 00 for **0x3F = 63** objects (the whole array, count is an immediate) |
| 57B6-57CA | `$1100,X` / `$1101,X` | **07** / 00 for the scene's own objects |
| 57CE-57E2 | `$1800,X` / `$1801,X` | 00 / **FF** |
| 57E6-57FA | `$1880,X` / `$1881,X` | 00 / **FF** |
| 57FE-5812 | `$1A00,X` / `$1A01,X` | **10** / 00 |
| 5816-582A | `$1A80,X` / `$1A81,X` | 00 / **01** |
| 582E-5842 | `$1780,X` / `$1781,X` | 00 / 00 |
| 5846-585A | `$1C80,X` / `$1C81,X` | **03** / 00 |
| 585E-5872 | `$0F80,X` / `$0F81,X` | **80** / 00 |
| 5876-588E | `$7F:0B00,X` | 00 / 00 — **dead**: 58BC-58CA clears `$7F:0580-1B3F` over it |
| 5892-58A6 | `$7F:0B80,X` | FF / FF — **dead**, same clear |

Then `58AA PLD`, and only then the `$7E:1900` and `$7F:0580` DMAs and `C0/58CD`'s constants.

### What it cost

Ported as `CtObjSceneDefaults` (move.c) after the user's 2026-09-07 report, *"when talking to GATO, nothing
happens and the battle does not start"*, from their own F5 capture: obj8 `$1000 = 82` where the oracle's own
loc-6 state reads `02`. Bit 7 of `$1000,X` is the op-0x0B PAUSE, and it is the guard `C0/5AD1` refuses
an ACTIVATE on. Twenty-five scripts set it and most never clear it (loc 130 pauses object 8 four times
and unpauses it never), so without this fill the bit rode from location to location on the same object
INDEX -- and GATO is object 8 of loc 6. Their capture also showed the two sibling symptoms: objects
past the scene's own count still carrying the *previous* location's live `$1100 = 04` instead of 80,
and `$1C00 = 00`, the byte `invoke_prologue_guards` recorded as "dead in our engine, a fresh `--scene 2`
reads 00". It was never dead; it was never initialised.

Of the thirteen live arrays, `$1080` is the one that moves a frame hash: it is the anim FIFO's
free/link pair, whose bit-7 marker `C0/5A0F` reads exactly as `C0/A9CD` reads `$0E01` for the draw
list, so zero meant "already queued and linked to object 0" on every fresh scene. Bisected on scene 5
at (376,488): `69CDD121` with that leg skipped, `6110E21A` with it. All thirteen now match the
oracle's own loc-5 image object for object.

---

## ⭐ $1BAB's seed is GONE (2026-09-12)

The cold-init line `m->s[0x0104] |= 192; m->s[0x1BA7] |= 14; m->s[0x1BAA] |= 4; m->s[0x1BAB] |= 1;
m->s[0x1BA8] |= 16;` carried a note saying these are UNCITED and need their own derivation. One of
them now has it, and it was not academic.

**`$1BAB` is ZERO on the oracle everywhere measured** -- boot f96/f100/f141, the settled title at
f2060, scene 433 at f2280, the attract's overworld, all eight `ow_w<n>.live` captures, and the
Truce-market door, where `--monitor 7E1BAB:1 --diff --writer` reports NO write across the whole
overworld transition. Combined with this census's own finding that the load never writes it, the
port's `|= 1` was permanent where the ROM's is always 0.

⚠ It broke the overworld's object bootstrap. World 0's bootstrap script tests it --
`27 AB 1B 01 08`, "if ($1BAB & 1) == 0, jump" -- so the stale bit sent the script down the wrong
arm, where it spawned one object and died instead of building the world
(sequence_table.md 9.6).

The other four ($0104, $1BA7, $1BA8, $1BAA) are UNTOUCHED: no measurement was taken for them here
and none of them has a demonstrated effect yet. $1BA7 is load-written anyway (the init runner's
16-bit `58F0 STZ $1B80,X` clears the $1BA6/$1BA7 pair), so its seed only survives to the first load.
