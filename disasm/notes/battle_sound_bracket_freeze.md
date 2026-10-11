# The battle's `70` sound bracket FREEZES the field — measured, not yet modelled

Context: the attract's demo battle (loc 0AD, `$2A1F` bit6). C0/1BAB posts `70 01` (C0/1BDF) on the way in and
C0/1BE6 posts `70 00` (C0/1C89) on the way out. Command `$70` is C7/08E3: when the mode changes it uploads the
6144-byte `$2F00` SFX set through C7/0922, 2048 three-byte units, each with a `C7/09DA` token handshake. The JSL
is synchronous: the main loop is parked until it returns; the NMI keeps running.

## 1. The cartridge

`ct.exe --hookpc <pc> --from <state> --frames N` (the per-frame `end=` is the CPU's bank/pc at the frame boundary,
`vPos` the scanline of the visit):

| post | state | JSL visit | frames ending in `$C7` | return visit (JSL+4) | elapsed |
|---|---|---|---|---|---|
| `70 01` | saves/attract_cena_inicial.state | C0/1BDF f1747 line 142 | f1747..f1760 | C0/1BE3 f1761 line 213 | 14×262+71 = 3739 lines |
| `70 00` | saves/attract_demo_battle_f1850.state | C0/1C89 f322 line 238 | f322..f336 | C0/1C8D f337 line 43 | 15×262−195 = 3735 lines |

≈ 5.10 M master cycles = 14.27 frames, the same both ways (the same set size). `$1E10/$1E11` go 1 → 0 at f322
(`--monitor 7E1E10:2 --writer`: C7:08F5).

⚠ `saves/attract_loc0AD_flea.state` CANNOT measure this: its `$1E05` is `$18` (bit7 clear), so C7/0157
`LDA $05 / BPL ->0192` skips every command in that run — the `70`s are consumed and never executed, no freeze.

Mailbox commands across the whole battle (attract_cena_inicial.state, `PEEK_WRITEWATCH=1E00:1`):
`70 01` f1747 · `18 08` f1778 (CD/3B79) · `19 F2` f1966 · `18 FF` f2125 (C1/40DD) · `70 00` f2173 · `10 18` f2195
(op EA via C0/0330's slot-1 run; 8 frames after the JSL returns) · `18 BA 80` f2273 (op E8).

## 2. Where the cartridge's freeze time goes (exit, `CT_HOOK_CYCLES=1 CT_HOOK_POLLHIST=1 --hookpc C709E5`)

Per frame inside the freeze (f324..f335): CPU main+poll ≈ 300–303 k, NMI ≈ 21–22.5 k, DMA ≈ 8.3 k / 10.4 k
alternating, HDMA ≈ 14.1 k, refresh 10,480. 1743 tokens over those 12 frames = **2077 CPU cycles per token**.
Poll histogram: mean 17.46 extra polls over 2049 tokens.

## 3. The port

`CT_SNDTRACE=1 CT_BATTLESND_LINE=<line>` on the live battle path (`--scene 433`, no `--selftest`):

| start line | `70 01` priced | `70 00` priced |
|---|---|---|
| 142 | 4,538,122 master, 13 NMIs, returns line 63 | 4,504,450, 13 NMIs, line 38 |
| 238 | 4,582,897, 12 NMIs, line 191 | 4,555,467, 12 NMIs, line 171 |

≈ 12.7 frames against the cartridge's 14.27 — **~11 % short**. Poll histogram (port, exit): mean 16.81.
Decomposition of the gap (≈ 1.6 frames):
- **Background** — the clock has two measured profiles, FIELD (NMI 12,097, no DMA, HDMA 14,310) and BATTLE
  (27,600 + 5,434 DMA, HDMA 17,904), and field.c never switches it (`CtAudioSetEngineMode` has no caller there).
  The cartridge's transition frames carry ≈ 55.9 k of background, neither profile: ≈ 0.8 frame.
- **CPU per token** — 1981 (port) vs 2077 (cartridge): ≈ 0.55 frame. About a quarter is the SPC answering
  later (0.65 histogram units); the rest is not attributed.

And the port applies NO freeze at all: the field-side posts are serviced and their `lastFrames` ignored, so
`70 00` and the slot-1 `10 18` land on the same field frame (f4087), where the cartridge has 22 frames between
them (15 frozen + the battle-return tail C0/0905..C0/0330, which the port does not cost either).

## 4. Why it is not ported yet

1. The JSL's START LINE depends on how long the battle-return code before it runs; nothing in the port derives it.
2. The transition's NMI/DMA background is state-dependent handler work (≈ 31 k a frame here) that the clock only
   knows as two per-engine measured constants; a third measured block for "battle transitions" would be a
   constant, not a derivation.
3. The surrounding timeline is off by more than the freeze: port `70 01` → `70 00` = 459 frames vs 426 on the
   cartridge (the battle itself runs 47 frames longer once the 14 frozen ones are removed), and after the
   battle `10 18` → `18 BA` = 96 vs 78.
