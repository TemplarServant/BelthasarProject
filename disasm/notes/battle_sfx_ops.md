# Battle SFX — the six anim-script sound opcodes (C1 anim engine)

Derived from the ROM + oracle. `dis.py <fileoffset>` (bank C1 = `0x1xxxx`, CD = `0xDxxxx`).

`src/battle.c` posts **no sound command at all** — a grep for `$1E00`/`CtAudioService`/any post in that
file returns nothing. So every battle sound effect is missing, which is the reported "battles have no
SFX while they happen (attack noises)". The battle MUSIC is separate and now works
(`audio_driver_mode.md`, commit 3d65224).

## 1. What the ROM emits, measured

Oracle, the forest battle from `saves/battle_re/forest_holdleft_pre.state`
(`--monitor "1E00:4" --writer --diff`, walk in with `40:240` then A-mash). `$1E00` reads 00 on every
row because the driver consumed the command inside the frame (writer `C7:0196` = its `STZ $00`
teardown); the id survives in `$1E01` and the PAN in `$1E02`:

```
f21    id 6F  pan 80      the encounter
f173   cmd 14 song 45     the battle music (the C0/1BAB arm)
f244   id 08  pan 80
f327   id 42  pan 80  |
f402   id 4F  pan 80  |   PC attack #1
f500   id FF  pan 80  |
f504   id 95  pan 78  |
f584   id 42  pan 80  |
f632   id 43  pan 7F  |   PC attack #2
f752   id FF  pan 7F  |
f756   id 95  pan 58  |
f893   id 42  pan 80  |
f957   id 4F  pan A0  |   PC attack #3
f1054  id FF  pan A0  |
f1058  id 95  pan A8  |
f1360  id 10  pan 00
f1395  cmd 11 song 08     the field song restored
f1438  cmd 81            C0:1C52 -- the field-mode restore chain
```

Three identical cycles, one per PC attack. Note the pan MOVES (80 / 78 / 58 / A0 / A8 / 7F) — it is
positional, not fixed.

## 2. Where they come from — the anim script, not battle.c logic

A ROM-wide scan for `JSL $C7:0004` (`22 04 00 C7`) gives bank C1 six sites: `1B50, 1B62, 40DD, 61B5,
61DF, 620F`. `C1/40D3` is the `LDA #FF / STA $1E01 / LDA #18` post — the `id FF` seen above. The other
three are the ANIM-SCRIPT opcodes, and they sit in the jump table `C1/7A6B` that
`src/battle.c`'s `battleAnimOpLenRom` ALREADY reads for opcode lengths:

| op | handler | command | id source | pan source | adv |
|---|---|---|---|---|---|
| `0x78` | C1/619E | `$18` | script operand | `#80` (centre) | 2 |
| `0x79` | C1/619E | `$19` | script operand | `#80` (centre) | 2 |
| `0x7A` | C1/61C1 | `$18` | script operand | 2nd operand -> `C1/762E` -> `$A2B0` | 3 |
| `0x7B` | C1/61C1 | `$19` | script operand | 2nd operand -> `C1/762E` -> `$A2B0` | 3 |
| `0x7C` | C1/61EB | `$18` | `CD/6D4C[$A4E7]` | `$1D0C,$A014` (combatant screen X) | 1 |
| `0x7D` | C1/61F5 | `$18` | `CD/6DAC[$A4E7]` | `$1D0C,$A014` (combatant screen X) | 1 |

The command byte is computed, not literal:

```
C1/619F  LDA [$E6],Y / STA $1E01          ; id from the script
         SEC / LDA $9895 / SBC #78 / CLC / ADC #18 / STA $1E00
         LDA #80 / STA $1E02              ; centre pan
         JSL C7/0004 ; INC $A00F ; LDA #02 / JMP C1/75BB
```

`$9895` holds the OPCODE, so `cmd = op - 0x78 + 0x18` for the 619E pair and `op - 0x7A + 0x18` for the
61C1 pair — i.e. the even op plays through `$18`, the odd one through `$19`. `$19` is the same panned
SFX command the FIELD uses at `C0/1B90` (`LDA $FC / STA $1E01 ; LDA $0A00,Y / STA $1E02 ; LDA #19`).

**This is exactly the shape of the overworld op-0x3B defect**: `battleAnimOpLenRom` already takes each
handler's length from its own `LDA #len / JMP C1/75BB` tail, so the script stays in sync and only the
EFFECT is dropped. Nothing desyncs; the sound simply never happens.

## 3. The other engine (bank CD) — same mechanism, a staging slot

Bank CD has its own interpreter with the same two op shapes, and it is worth recording because it
shows the ROM's staging model explicitly:

```
CD/18CF  LDA [$40] / STA $CD24 ; LDA $5DA0 / TAX / STX $CD25 ; INC $CD23   ; id + positional pan
CD/1923  LDA [$40] / STA $CD24 ; LDX #0080 / STX $CD25       ; INC $CD23   ; id + centre pan
```

and the FLUSH, once per frame:

```
CD/3B62  BEQ ->3B70                       ; $CD23 == 0 -> nothing pending
CD/3B64  STZ $CD23 / LDA $CD24 / LDX $CD25
CD/3B71  STA $1E01 ; LDA #18 / STA $1E00 ; TXA / STA $1E02 ; JSL C7/0004
```

So: an op STAGES `(id, pan)` into `$CD23/$CD24/$CD25` and a separate pass flushes at most one per
frame. The C1 handlers post inline instead. Which of the two engines drives a given battle is NOT
derived here.

## 4. What the port needs (not attempted)

- `src/battle.h` has **no sound hook at all**, and `g_fieldAudio` is a file-static in `field.c`. So
  battle.c cannot reach the SPC without new plumbing — the natural shape is the one `ct_vm.c` already
  uses: a `CtSoundSink` installed by field.c (`CtSceneSetSoundSink`), mirrored for the battle module.
- The six opcodes then stage `$1E01`/`$1E02`, compute `$1E00` from the opcode, and post.
- `$A4E7` (the 0x7C/0x7D table index) and `$A2B0` (the 0x7A/0x7B pan) are NOT derived.
- Whether our engine runs the C1 or the CD interpreter for a given anim is NOT derived.

## 5. Open

- ~~`id FF` (C1/40D3) fires once per attack cycle in the trace above~~ — **PORTED 2026-08-22**, together
  with two more lifecycle posts and the exit chain's fifth link, all found by a read-only audit:
  | post | ROM | when | oracle |
  |---|---|---|---|
  | `#08` pan 80, cmd 18 | `CD/04FD` (vector `CD/000C`, called from the boot sequence `C1/0038` right before the main loop `CD/0012`) | the entry→combat handoff = `battleCombatStart` | `f305 7E1E01=08*(CD:3B74) 7E1E02=80*(CD:3B7D)`, ~74f after the battle-song command, the frame before `$CA1E/$CA1F` are seeded |
  | `#FF` cmd 18, **no pan** | `C1/40D3-40DD`, the frame `C1/40CE LDA $9876 / BEQ` falls through | once per action = our act-end | `7E1E01=FF*(C1:40D8)` at f535/758/975/1198/1422/1604/1831/2104, `$1E02` unchanged each time (86 after a pan-86 post, B0 after B0) |
  | `#10` pan 00, **cmd 81** | `CD/039E`, via `CD/03D3` gated `$2A1F & 40 == 0` | the victory hand-off | `f2264 7E1E01=10*(CD:03A8) 7E1E02=00*(CD:03AB)`, 35f before the field's C0/1C16 chain |
  | cmd `$83`, id 00 pan FF | `C0/1C69-1C78`, the 5th link of the restore chain | after the `$82` link | `f2309 7E1E01=00*(C0:1C6E) 7E1E02=FF(C0:1C73)` |
  `#FF` is posted through a pan-less poster (`battleSfxPostNoPan`) because writing `$80` there would
  invent a byte the ROM does not write. What `$FF` means to the Akao driver (a stop? a channel reset?) is
  STILL not derived — only the post is. Pinned by `field-battle-lifecycle-sfx` (3 mutations, each deleting
  one post together with its trace line, all KILLED — the trace is COUPLED to the post's return value, so a
  dropped post cannot leave a surviving line).
  Still missing from the same audit, not done here: the command-menu chime/blip (`C1/1B41`/`C1/1B55`, 30
  call sites), and every PC TECH's sound opcodes (the CE scripts carry `78`/`7A` ops that no PC-side path
  executes — the PC route runs no interpreter).
- The `$9895` command arithmetic is transcribed, but no capture exercises the `$19` (odd) ops, so the
  odd half is disassembly-only.
