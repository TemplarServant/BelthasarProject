# $7E/$7F WRAM LIFETIME — what a location change actually does, and why the port's scene-scoped WRAM is wrong

Derives the lifetime of WRAM across a **location change** (field→field door, field→overworld exit,
overworld→field entry), to settle a structural question the port had only named in a comment:
`src/field.c` documents that the sound mailbox `$7E:1E00-$1E03` "is CPU-GLOBAL WRAM that outlives any
scene, whereas ours is tied to the scene VM's lifetime", and papers over the consequence with a NULL
guard. This file establishes the ROM side of that claim, and states plainly why the corresponding
structural fix is **NOT executable yet**.

**Method note, and it matters.** Three derivations ran in parallel (ROM disassembly / oracle
measurement / port audit) and every claim was then handed to an independent adversarial verifier
instructed to refute it. **Exactly one claim survived the pass as originally written.** Most of what
is recorded below comes from the *counter-evidence the verifier produced while refuting* — spot
probes, not a survey. Sections are marked accordingly. Two confident, plausible, wrong claims are
recorded in §4 precisely because they were nearly shipped.

---

## 1. THE HEADLINE — a location change does NOT wipe $7E

The three full-WRAM DMA wipes live in exactly one routine, **C0/000E**. The question was whether a
location change reaches it. It does not.

**PROVEN (survived verification):** `$7F:0000-$7F:01FF` — the global story flags — are **never
touched** by a location load. Sentinel `0xA5` written over `$7F:0040-00FF` and `$7F:0100-01FF`, then
a location change: **100.0% survival in both windows, control and teleport**. No store to `$7F:00xx`
exists anywhere in the C0/0066 load chain.

**MEASURED (verifier spot probes).** Four transitions censused — a door, its reverse, a field→
overworld exit (`011→1F0`), an overworld→field entry (`1F0→011`):

| window | door | reverse | exit | entry |
|---|---|---|---|---|
| `$7E:2000-28FF` | 92 | 44 | **0** | 48 |
| `$7E:2600-2FFF` | 1 | 0 | **0** | 4 |
| `$7E:2A00-2FFF` | **0** | **0** | **0** | **0** |

A **field→overworld exit changes zero bytes in the entire `$7E:2000-2FFF` page.** The changed
addresses are exactly `$2102-2139 + $2302-2339 + $29AE` (door), `$21xx + $23xx` only (reverse), and
`$2102-2119 + $2302-2319 + $29D4/D5/E0/E1` (entry). The `$21xx`/`$23xx` blocks are a verified
byte-for-byte mirror (`b[i] == b[i+0x200]` for every changed byte, all four pairs); what they *are*
was not derived.

**Small constant fills the load does perform** (disassembled, six of seven — the seventh is refuted
in §4):

| ROM | fill |
|---|---|
| `C0/7155` | `$7E:0B80-0B87` = 0x80 |
| `C0/6F79` | `$7E:0B88-0BA5` = 0x80 (`SEP #10` + `LDX #1E`) |
| `C0/E935` | `$7E:0BC0-0BC7` = 0x80 (D=$0B00) |
| `C0/5929` | `$7E:0920-092F` = 0xFF (sixteen `STA $0920..$092F`) |
| `C0/B204` | `$7E:0E00-0EFF` = 0x80 (32 port bytes, then `MVN` #001F/#003F/#007F self-doublings) |
| `C0/A7E9` | `$7E:0F00` = (00,FF) per object → `$0F00-$0F27` = `00FF` ×20 |
| `C0/28AA` | `$7F:1CC8-1CE7` = 0x8080 |

**Plain answer.** On a location change the ROM never wipes $7E. It performs the handful of constant
fills above, rewrites the object/render tables and the map-adjacent pages, mirrors two ~56-byte
blocks into `$21xx`/`$23xx`, and touches six individually-identifiable bytes in the save block. The
64 KB of $7E is **one continuous, process-lifetime object**.

---

## 2. THE MAILBOX SURVIVES — the port's comment was right

`$7E:1E00-$1EFF`: **250 of 256 bytes byte-identical** across the sampled transitions, with writer PCs
in **bank $C7** (the Akao driver itself). The mailbox proper is `$1E00-$1E03` per `C0/1B53`.

So in the ROM the mailbox page **is simply always there** — on the overworld exactly as in a field.
The port's scene-scoped `$7E` makes it *conditionally absent*. That is a **lifetime divergence, not a
missing feature**, and the NULL guard at `src/field.c` suppresses the crash without restoring the
mailbox.

Not closed: the instruments are frame-granular (`--monitor --writer` keeps only the last writer per
region per frame, and only on a changed row), so a **sub-frame wipe-and-restore cannot be formally
excluded**. Closing it needs a per-CPU-step write watch over the full page. None was observed.

---

## 3. THE WIPE *IS* REACHABLE IN NORMAL PLAY — via load-game, with no reset

"C0/000E is entered exactly once, from RESET" was **refuted**. A relative-branch scan of bank C0
(opcode `0x82` — invisible to a JSR/JMP/JSL/JML xref, which is why it had been missed) finds a second
entry that re-enters cold init with a fresh stack:

```
C0/02C3  JSR C0/0B4E
C0/02C6  LDX #06FF
C0/02C9  TXS                 ; fresh stack
C0/02CA  BRL  -> C0/000E     ; the full WRAM wipe
```

Reached from **C0/0283**, which dispatches on **`$7F01EC`** (0 → `BRL 0304`; **1 → 0298 → … → 02CA →
000E**; 2 → 02CD). `C0/0283` is called from `C0/18C1`, i.e. right after `JSL C1/0000` in the field
menu handler (`C0/188C-18CD`). And `$7F01EC` is set to 1 by the menu bank: `STAl $7F01EC` at
**C1/813F** (`LDA #01` at C1/813D), also C1/817A, C1/81F9, C1/8357.

**Load-game from the in-game menu wipes all of WRAM with no reset.** Any future host-owned $7E must
model that path, not just boot.

(RESET path confirmed separately: `C0/FFFC = FF00` → `FF00: SEI / CLC / XCE / JML FD/C000` → `JML`
from FD/C0D3 into C0/000E.)

---

## 4. TWO CLAIMS THAT WERE WRONG — recorded because they were nearly shipped

**(a) `C0/B246` does NOT fill `$7E:0F01+` with `FF 00 00 00` per object.** The raw bytes
(`[28 ROM bytes at 0xB246: python tools/dis.py 0xB246 28]`) show
`LDX #00FF` / `LDY #0000` are **16-bit** — there is no `SEP #10`, unlike the analogous loops at
`C0/5709` and `C0/A7E9`. With D=$2100 a 16-bit `STX $80` writes the low byte to WMDATA (`$2180`) and
the **high byte to WMADDL (`$2181`)**, resetting the WRAM port address every iteration. Decisive
measurement: 0xA5 sentinel over `$7E:0F00-0FFF`, a 439→2 teleport, then `--read wram:7E0F00+96` →
`00FF` ×20 followed by `00A5 00A5 …`. **`$7E:0F29` is still 0xA5.** Nothing wrote past `$0F28`.

**(b) "`$7E:2300-29AD` and `$7E:29E2-2FFF` survive untouched" is false.** Sentinel across 439→2
(`--settle 250`): `$7E:2400-28FF` → 99.6% survival with a **5-byte kill run at `$7E:2857-285B`**;
`$7E:2900-29FF` → 96.9% with kill runs at `$2989`, `$29AE`, `$29C8-C9`, `$29D4-D5`, `$29E0-E1`.
`$7E:2989` sits *inside* the asserted-untouched window and is written by the load (`0C5E: STA
$7E2989`). The `$29E2-2FFF` half of the claim does hold (100.0% over `7E2A00-2F7F`).

What *is* confirmed about the party: `C0/0C34: LDA $7E2980 / STA $94 / LDA $7E2981 / STA $95 / LDA
$7E2982 / STA $96` — the active-party triple is **read, never written**, and survives as 0xA5. In the
ROM the party simply persists in WRAM; **the port's `g_carryParty` exists only to simulate that.**

---

## 5. THE PORT SIDE (verified facts only)

- `src/ct_vm.h:224` / `src/ct_vm.c:3132` — `CtSceneMem7E(sc)` returns `sc ? sc->mem.s : NULL`
  (NULL-safe). **33 call sites across 31 lines, all in `src/field.c`** (line 4825 carries three).
- `src/ct_vm.c:3137` — `CtSceneMemGlb(sc)` returns `sc->mem.g`, **unguarded**. **11 call sites, all in
  `src/field.c`**: 1586, 4118, 4341, 4832, 5788, 6191, 6197, 6213, 6216, 6822, 6883.
  (The port audit claimed five sites and cited a line — 4883 — that is `int nw = 0, nh = 0; uint8_t*
  nprio = NULL;`. The verifier's grep is the count above.)

**A NULL guard on `CtSceneMemGlb` is NOT the fix and should not be added on its own.** Its eleven
callers dereference the result; returning NULL converts a crash at a known line into a crash at
eleven unknown ones. The asymmetry with `Mem7E` is a *smell that locates the question*, not a defect
with a one-line answer.

---

## 6. WHY THE STRUCTURAL FIX IS BLOCKED — read this before attempting it

The direction is well-supported by §1-§3: the ROM's $7E is process-lifetime, wiped only by C0/000E
(boot, and load-game via `$7F01EC==1`). A location change writes a bounded, enumerable set of spans
into a **surviving** 64 KB. The port gives that same 64 KB *scene* lifetime. Per the mandate a
structural divergence gets a structural fix — not more NULL guards and not more carry statics.

> ## ⚠ THIS BLOCKER WAS WRONG — RESOLVED 2026-07-25, and the routine was misidentified
>
> **`C0/5CC7` is not the object-init runner and dis.py does not desync in it.** It decodes cleanly
> with **m=1 (8-bit A) / x=0 (16-bit index)** and is a **script SANITY CHECK** ending in `RTS` at
> `C0/5CEB`; there is no `BRL C0/5CEC` in it. The "desync" was **my own wrong width assumption**:
> read as x=1, the bytes `A2 F0 01` decode as `LDX #$F0` plus a stray `01`, when the instruction is
> `LDX #$01F0`. Every byte after that was garbage, and I recorded the garbage as a property of the ROM.
>
> **The real object-init runner is `C0/58EA`–`C0/5922`, inside `C0/5709`.** For X = 0,2,…,2N−2 it
> zeroes `$1B01,X` and `$1B80,X`, skips the object on a negative test, else interprets fn0 through
> the `$5D6E` table (opcode 0 = end) and stores the resumed IP back to `$1180,X`; `C0/595C` then runs
> object 0's fn1 pointer (`$7F2003`). `$BD` = N<<1.
>
> **Load-bearing detail, easy to miscode:** `REP #20` at `58E2` sets m=0 and is **not** restored until
> `SEP #20` at `58FE`, so `LDA $1100,X` at `58F3` is a **SIXTEEN-BIT** load and the `BMI` tests **bit
> 15 = bit 7 of `$1101,X`**, not bit 7 of `$1100,X`. Porting it as `if (ram[0x1100 + 2*i] & 0x80)
> skip;` would skip **every** object on the first pass — the fills put `0x0080` and `0x0007` in those
> words, both positive. The gate is live, not vestigial: the loc-002 post-load image holds
> `0007 0000 0180 0280 0380 0480 0580 0680 2504 3B04 …`, of which `B004` and `AF04` are genuinely
> negative.
>
> **Still PARTIAL, so the ownership move is still not writable.** What remains: the `$5D6E`
> init-opcode table is not enumerated, so which objects get `$0C00`/`$0C80`/`$0D00`/`$1600`/`$1680`/
> `$1700` written on a given load is script-dependent — the port must carry the previous location's
> bytes and let the init pass write whatever it writes, and must **NOT** synthesize a clear list.
> The **overworld leg is entirely underived** (`C0/0066` branches `CPX #01F0 / BMI / JML C2/0000` for
> loc ≥ `$1F0`, and bank C2 **reuses `$7E:0F00` as a sort array** indexed to `$F2`), and there is
> still no natural-door measurement.

**The original (superseded) blocker text:** which ROM routine clears the per-object tables on a
location load is NOT DERIVED. The chain traces `C0/56D4` → `JSL C3/0002` (script decompress to
`$7F2000`) → `JSR C0/5CC7` → `BRL C0/5CEC` (the object-init runner), and **`tools/dis.py`'s linear
decode desyncs inside C0/5CC7**.

Today `fieldFree` + a fresh `CtScene` implicitly zeroes every object table. Move $7E to the host
without resolving C0/5CC7 and you must supply an explicit clear list — and **any clear list written
down today is INVENTED**. That is exactly the "heuristic standing in for a real ROM mechanism"
violation the mandate names. **Resolving C0/5CC7 / C0/5CEC is the prerequisite, not a refinement.**

Two measurements already contradict the tempting "the init pass rewrites everything anyway"
assumption, and must be explained before any clear list is credible:

- the `$7E:0F00` overwrite spans **63 bytes (`$0F00-$0F3E`)**, not the 81 predicted by A7E9 ∪ B204 —
  and B204's per-object leg is refuted outright (§4a);
- the `$7E:1100` overwrite runs contiguously to **`$11A7`**, but `C0/57A0`'s 63-entry fill stops at
  `$117D` and `C0/5731`'s pointer fill starts at `$1180`. **Nothing identified writes `$7E:117E/7F`.**

**Also forbidden in this work:** an overworld-shaped special case for the mailbox (`if (!sc) …`) — the
ROM has no overworld-vs-field branch in mailbox servicing, so that is a scene-gate by another name;
and porting an overworld song path from `field.c`'s comment about C2/3009, which has never been
disassembled and is hearsay until it is.

---

## 7. STILL NOT DERIVED

> ## ✅ ITEMS 1-4 ARE CLOSED — see `wram_load_census.md`
>
> A per-CPU-step **write census** of the whole transition (`ct.exe --loadcensus`), reconciled against
> a before/after image of all 128 KB, settled all four. The headline, and the reason none of them
> could be settled before: **not one of the location load's clears is an instruction.** All four go
> through `C0/2DF1`, which points DMA channel 7 at `$2134` (the PPU multiplier forced to 0) and lets
> the transfer write the zeros — invisible to a `STA`/`STZ` xref, to `--monitor --writer`, and to
> `cpu->write` itself.
>
> | # | setup | call | window | bytes |
> |---|---|---|---|---|
> | 1 | `C0/7F87` | `C0/7F95` | `$7E:1D00-1DFF` | 256 |
> | 2 | `C0/5709` | `C0/5717` | `$7F:0200-03FF` | 512 |
> | 3 | `C0/58AB` | `C0/58B9` | `$7E:1900-19FF` | 256 |
> | 4 | `C0/58BC` | `C0/58CA` | `$7F:0580-1B3F` | 5568 |
>
> Identical on two different source states, so it is a clear LIST, not an observation. Plus the
> per-object `58ED STZ $1B01,X` / `58F0 STZ $1B80,X` in the init runner. **§6's "any clear list
> written down today is INVENTED" no longer applies — this one is measured.**
>
> - **item 1** → the table above.
> - **item 2** → the premise does not reproduce. The write set is `$0F00-$0F27` + `$0F80-$0FA7`
>   (40 + 40 B) on both source states; "63 bytes `$0F00-$0F3E`" was a changed-byte count read as a
>   write extent. §4a is CONFIRMED from the writer side, and the cause is exactly one token: `C0/A7E9`
>   and `C0/585C` do `SEP #10` before their port stores, `C0/B246` does not.
> - **item 3** → **nothing writes `$7E:117E/117F`**, and they do not change. §6's "runs contiguously
>   to `$11A7`" is refuted: there is a real two-byte hole between `C0/57A0`'s fill and `C0/5731`'s.
> - **item 4** → **YES, `$7F:0200-03FF` IS cleared** (clear #2). `events.md:40` is correct. ⚠ The
>   CPU-side census alone would have said "no, only 2 bytes" with full confidence; only the DMA leg
>   answers it.
>
> The remaining §7 material below (unlabeled memory, coverage gaps) still stands, except that **one
> natural door is now captured** — `saves/truce_market.state --btnscript "0:10,20:60,0:200"` walks
> loc 011 → 1F0 through the real exit at f43.

**Blocking the implementation:**
1. ~~**Which routine clears the per-object tables on a location load**~~ **CLOSED** — four DMA clears
   via `C0/2DF1`, listed above.
2. ~~Why the `$7E:0F00` overwrite spans only 63 bytes~~ **DISSOLVED** — it does not; that was a
   changed-byte count, not a write extent.
3. ~~What writes **`$7E:117E/$117F`**~~ **CLOSED (negative)** — nothing does.
4. ~~Whether the ROM explicitly clears **`$7F0200-$7F03FF`**~~ **CLOSED (yes)** — 512-byte DMA at
   `C0/5717`.

**Unlabeled memory:** `$7E:2102-2139` / `$7E:2302-2339` (the mirrored pair, the *only* thing in
`$7E:2000-28FF` a location change touches — writer PC unchased); `$7E:29D4/D5/E0/E1` (changed only on
overworld→field entry: `C0 83 → 20 89` and `C0 83 → E0 9B`; the port never touches them); what writes
`$7E:1F00-1F3F` and `$7E:1F80-1F8F`; the identity of `$7E:1E1C-1E86`; and semantic labels for
`$7E:0E05-103F`, `$7E:1D04-1E03`, `$7E:7043-7F8C`, `$7E:8696-869A`, `$7E:D882-D8B5`, `$7F:0C14-1532`.

**Coverage gaps — the honest limits of the measurement:**
- **No natural door transition was ever captured.** Every field-load measurement used `--teleport …
  --method jmp`, which pokes `$0100-$0104` and enters at `C0/0066` — two JSRs late, skipping
  `C0/005D`'s `JSR C0/0B4E / JSR C0/0B64 / JSR C0/0B75`. Those appear to write only hardware registers
  plus the JML stubs at `$7E:0500-0507`, so the map *should* match, but **that is not proven by
  walking through an actual door.**
- **Transitions never sampled:** battle entry/exit, time-gate / Epoch world change, menu-driven warp,
  and any destination whose location record has bit7 set (the "declines the song" BMI path at
  `C0/1B7D`). All four sampled transitions are 1000 A.D. doors, and in three of them the song id
  happened not to change.
- Whether `$7E:1E00-1EFF` is wiped and exactly restored *within* a single frame (§2).
