# A present character drawn by NOBODY — the entrance suppress mask was party-blind

Every battle opens with an entrance sequence. For its first ~176 frames the port suppressed OBJ
palettes 5 and 6 in the stance layer so the entrance layer could draw the morph instead — but the
suppression did not ask who was in the seat, and the drawing did. A seat-1 character who is not
Marle was hidden by one layer and declined by the other.

This note covers the **minimal fix**. The real port (arming the ROM's pose window 0x30 and deleting
the keyframe bake) is a separate, larger change; §4 records what it needs.

---

## 1. THE ASYMMETRY

```c
// src/field.c -- the SUPPRESSOR, before
static int battleEntranceMask(const CtBattleHost* h) {
  int t = h->frames + CT_ENT_BOOT_F, mask = 0;
  for (int k = 0; k < CT_ENT_NKF; k++)
    if (t <= ctBattleEntranceKf[k].endF) mask |= (1 << (ctBattleEntranceKf[k].pal & 7));
  return mask;                       // <- keyframe TIMINGS only. No party.
}

// ...and the DRAWER, which does ask:
if (mem && ((mem[0x2980 + seat] & 0x80) || (mem[0x2980 + seat] & 7) != seat)) continue;
```

`battleStanceRender` honours the mask (`if (suppressPal & (1 << pal)) continue;`), so a palette in
the mask is skipped there; `battleEntranceRender` then skips the same seat for a different reason.
Nobody draws it.

### The window, from the baked data

`CT_ENT_BOOT_F` is 222 and `t = h->frames + 222`, against `src/data/battle_entrance.h`:

| pal | seat | keyframe span | suppressed while |
|---|---|---|---|
| 5 (Crono) | 0 | startF 290 … endF 344 | `frames <= 122` |
| 6 (Marle) | 1 | startF 290 … endF 398 | `frames <= 176` |

So **the first 176 frames of every battle**, for whoever is in seat 1.

---

## 2. THE FIX

The mask now applies the renderer's own already-derived gate — present (`$2980+seat` bit 7 clear)
AND the seat holding the character the bake belongs to (`(… & 7) == seat`). Nothing new is derived:
it is the same condition, asked in both places instead of one. A seat the entrance layer will skip
is no longer hidden from the layer that would have drawn it.

---

## 3. MEASURED, on the frame a pinned check already renders

`field-battle-no-gato-in-other-fights` renders `bat9.raw.wram` at `CT_BATTLE_RENDERFRAME=40`.
bat9's party is `$2980..82 = 00 02 04` — Crono, **Lucca**, Frog — and t = 40 + 222 = 262, inside
both spans, so the old mask was `0x60`. That check's pinned frame therefore **recorded the bug**.

Before vs after, same frame, differing pixels only:

```
frame 256x224   differing pixels: 266
  bounding box  x 118..132   y 92..123   (15x32 -- one character)
  non-black pixels in that box:  BEFORE 146   AFTER 412
```

Lucca appears. The check's hash moves `B9EAC66F -> D9003ED9` and is re-pinned as a **corrected
baseline**, with the cause named in its notes so nobody re-pins it back.

Nothing else moves, and that was checked rather than assumed: every other fixture rendered inside
the window has `charId == seat` (`00 01 80`) or an absent seat, and the absent-seat test in
`battleStanceRender` runs before the suppress test anyway.

---

## 4. WHAT THIS IS NOT — the real port, still open

`CtBattlePcLadderArm` is called in exactly two places (`src/battle.c:7101`, `src/field.c:6751`) and
both pass window **0x28** (victory). Windows **0x30** (entrance) and **0x20** (defeat) are never
armed, so the entrance is replayed from six baked keyframes with absolute oracle frame numbers
instead of from the ROM's per-charId pose ladder.

Derived this session and NOT yet acted on: the battle boot is
`C1/0023 JSL CC/E2E4 / JSR C1/0299 / JSR C1/283D / JSL CC/ED87 / JSR C1/34A7 / JSL CD/0003 /
JSL CD/000C / JSL CD/0012 (loop) / **C1/0042 JMP C1/34DB**` — and the raw bytes at file offset
0x10036 end `… 4c db 34`, with `JMP C1/34DB` the only reference to that routine in bank C1.
`src/battle.c:6680-6683` transcribes exactly that sequence **and stops one instruction short**, at
the `JSL CD/0012` loop. So the arming site is identified; wiring it means deleting
`src/data/battle_entrance.h` and the whole `battleEntranceRender`/`battleEntranceMask` pathway this
note just repaired, which is why it is a separate change.
