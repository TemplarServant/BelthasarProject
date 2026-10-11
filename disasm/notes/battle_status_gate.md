# Enemy status infliction: the blocker was retired eight days before anyone noticed

The physical status rider fired only when `CT_BATTLE_STATUSTEST` was set, and the comment above it
explained why: the engine "does NOT model the staged attack header ($AEE6/$AE4D), so the NATURAL
gate (AE4D bit2) is UNREACHABLE". That stopped being true at commit **4d0f652**, which stages the
whole 12-byte header. The three bytes the ROM reads had been sitting in WRAM, unread.

---

## 1. THE MECHANISM, re-decoded

The physical hit-tail, `C1/D306`, with `m=1 x=1`:

```
1D335  JSR C1/DCAD            ; apply the damage (incl. the wake-on-hit &0xF9)
1D338  LDA $AE4D
1D33B  AND #04
1D33D  BEQ ->1D345            ; <- THE GATE
1D33F  JSR C1/D14F            ; stage the rider's inputs
1D342  JSR C1/E976            ; roll + inflict
1D345  LDA $AE4D / AND #02 / BEQ ->1D34F
1D34C  JSR C1/E9D5            ; the SIBLING rider on bit1 (charm/steal), still env-gated -- see §4
```

The stager, `C1/D14F`:

```
1D14F  TDC / TAX / STX $16 / STX $18     ; clear both, 16-bit
1D155  LDA $AEE7 / STA $16               ; ACCURACY
1D15A  LDA $AEE8 / STA $18               ; MASK
1D15F  RTS
```

The roll and inflict, `C1/E976`:

```
1E978  LDA #64 / JSR C1/AF22   ; rand(0..99) on the $26 stream -- UNCONDITIONAL once the gate is set
1E97D  STA $0E
1E97F  LDA $16 / CMP $0E / BCC ->1E9A2   ; accuracy < roll -> nothing
1E985  LDA #01 / STA $16                 ; <- the CATEGORY
1E989  JSR C1/DBAA
```

### ⭐ `cat = 1` is the ROM's own literal, not a hardcode

A prior audit filed it as one. `$16` is **one direct-page byte with two meanings in sequence**: it
holds the ACCURACY for the compare at `1E97F`, and is then overwritten with `#01` at `1E985` before
`C1/DBAA` reads it back as the CATEGORY. The port's `const int cat = 1` was right all along; only
the mask and the accuracy were invented.

### The inflict, and the timer condition

`C1/DBAA` indexes `X = block + cat`, tests immunity `$5E4F,X & $18`, and ORs `$5E4A,X |= $18` —
i.e. `block[0x22+cat]` and `block[0x1D+cat]`, exactly what the port already had. Then:

```
1DBE1  STA $5E4A,X            ; the bit is set FIRST
1DBE4  JSR C1/E006            ; ...and only then is the timer armed
1E01B  LDA $18 / BIT #F0 / BNE ->1E024 / JMP C1/E0F4
```

So **a mask with no bit in `$F0` takes the untimed path**. While the mask was hardcoded `0x04` the
old note ("mask 0x04 arms NO timer") was right by accident; with the mask taken from `$AEE8` it
becomes a real condition.

---

## 2. WHAT WAS DONE

* The gate is now `s[0xAE4D] & 0x04`, the accuracy `s[0xAEE7]`, the mask `s[0xAEE8]` — all three
  staged since 4d0f652 (`for (i=0;i<12;i++) s[0xAEE6+i] = rom[tbl + idx*12 + i]` and
  `s[0xAE4D] = s[0xAEED]`, on the PC and enemy paths alike).
* `CT_BATTLE_STATUSTEST` survives as an **override** (forced accuracy, mask forced to `0x04`) so the
  checks pinned on the forced path stay byte-identical.
* The two comments asserting the retired premise are rewritten rather than left to mislead a fourth
  reader.

### ⚠ Two things deliberately NOT tidied

* **No `&& mask` guard on the call.** The ROM does not test the mask before rolling — `E976`'s first
  act is the `rand(0..99)` draw. Skipping it for a zero mask would swallow a `$26` draw and shift
  the RNG stream, the same class of mistake that once disabled every PC hit roll in the forest fight
  (the `$B2CA` note in battle.c). A zero mask simply ORs nothing.
* **The `$F0` timer arms are not ported.** `C1/E006`'s per-bit branches write `$AF27/$AF32/$AF3D/
  $AF7F`, which nothing in this engine ticks. The bit is still set (the ROM sets it first,
  unconditionally at `1DBE1`), and the omission is **counted** — `g_btStatusTimedUnported`, surfaced
  as `timed-unported=` in the trace — rather than silently inflicting a status that never expires.

---

## 3. HOW IT IS GRADED — an invariant, because no fixture reaches a real one

Neutrality was measured **before** the change: with the real gate, the GATO `--battletest` fires the
rider **0** times and still reports `battletest: PASS (staging 18/18, ...)`. The forest fixture is
the same. So nothing in the suite moved, which is exactly the shape of an ungraded change.

The pin added to `field-battle-gato-attack-sfx` and `field-battle-hetake-attack-sfx`:

```
status-gate AE4D=10 fire=0 acc=00 mask=00 src=header timed-unported=0
```

* `AE4D=10` is the **liveness** half — a NON-zero staged byte proves the 12-byte header is really
  being read, not a hardcoded zero.
* `fire` is the **decision**, not the byte. Pinning `AE4D` alone would leave a mutated always-on
  gate green; mutating `gate = 1` turns **both** checks red.

**132 shipped attack records DO set the bit** — 102 in `CC/89C6` and 30 in `CC/7AC9`, with real
(accuracy, mask) pairs, e.g. record 7 = acc 100 / mask `0x04`, record 2 = acc 25 / mask `0x80`. So
the subsystem is genuinely live for them; the repo simply owns no capture of a fight that uses one.

---

## 4. OPEN

* **The `$F0` timer arms** (`C1/E006` from `1E024`) and the tick that would expire them. Until then a
  timed status inflicted by one of the 132 records stays until something else clears it, and the
  counter says so.
* **The sibling rider on `$AE4D` bit1** (`C1/E9D5`, the charm/steal lane at `1D34C`) is still on
  `CT_BATTLE_CHARMTEST`. ⚠ **THE SENTENCE THAT USED TO BE HERE — "the same fix applies" — IS WRONG,
  and I checked before repeating it.** The gate is indeed `$AE4D & 0x02`, but the CHANCE is not a
  header byte the way the status accuracy is. `C1/E9D5` reads it out of **`$16`**, and nothing
  stages `$16` for the charm: the only stager in the tail, `1D33F JSR C1/D14F`, sits INSIDE the
  bit2 arm.

  ```
  1D306  LDA $AE4D / AND #20 / BNE ->1D32C
  1D30D  JSR C1/D174            ; stages $16 for...
  1D319  LDA $16 / BNE ->1D32C  ; ...the HIT/MISS test
  1D338  LDA $AE4D / AND #04 / BEQ ->1D345
  1D33F    JSR C1/D14F          ; $16 := $AEE7  (accuracy)   <-- inside the bit2 arm
  1D342    JSR C1/E976          ; $16 := 1 if the roll lands (the category)
  1D345  LDA $AE4D / AND #02 / BEQ ->1D34F
  1D34C    JSR C1/E9D5          ; reads $16 as the CHANCE
  ```

  So by `1D34C` the byte has carried three different meanings, and which one it holds depends on
  whether bit2 was set and whether the status roll landed. That is not a quirk one can round off:
  **the bit1-without-bit2 case is common in the shipped data** — 42 PC records and 20 enemy records
  set bit1 with bit2 clear, so a naive "pass `$AEE7` as the chance" would be wrong for 62 records.

  Porting it means modelling `$16` as a live direct-page byte across D174, the hit test, D14F, E976
  and DBAA — a real derivation, not a rider on this one.
* A capture of a fight where an enemy uses one of the 132 status records would upgrade the pin from
  an invariant to a behaviour.
