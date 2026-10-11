# BATTLE HUD — the ENEMY-NAME box is built ONCE at staging and never re-read

User report (translated from Portuguese): *"when the battle ends and shows the xp gained etc, the bottom menu should disappear"*
— at the victory results screen the bottom box looked wrong and the user read the empty box as "this menu
should have disappeared".

**What the oracle actually does at that moment:** the bottom strip STAYS, and its left box still reads
`Hetake` — with `$AF02` all-FF (every enemy dead) and `$96F8` all-00 (every enemy delisted). Our port
re-derived the name every frame from the *first ALIVE staged slot*, so the box went EMPTY the moment the
last enemy died. The user saw an empty box where the ROM shows a name.

Companion to `battle_hud_render.md` (the strip's chrome/layout, Q3a "box0 r2 cols 1-11 = the enemy name")
and `battle_results_window.md` (§V1 already recorded "the bottom HUD strip stays visible beneath the
results box the whole time").

---

## 1. The mechanism (ROM `CC/ECB2 .. CC/ED86`, `tools/dis.py 0x0CEC90 0x50 0 0` + `0x0CECDD 0x50 1 0`)

```
CECB2  TDC/TAY; STY $80; STY $82; STY $84; STY $86; STY $88   ; counts[0..2]=0, $86 = list cursor, $88 = slot
CECBE  loop: LDX $88
CECC0    LDA $96F8,X / BEQ ->CECEB          ; slot PRESENT?   ($96F8 = $96F5[3+slot]; CC/EC23 `INC $96F8,X`)
CECC5    LDA $9FFA,X / BMI ->CECEB          ; slot HIDDEN?    ($9FFA = $9FF7[3+slot], and CC/EAB5 is
                                            ;   `LDA $29C6,X / AND #80 / STA $9FF7,Y` = staged rec byte +7 bit7)
CECCA    REP #20
CECCE    .cmp: LDA $9853,Y / CMP $98FF,X    ; the slot's enemy id (u16) vs the 3-entry DISTINCT list
CECD4          BEQ ->CECE6                  ;   already listed -> just bump its count
CECD6          INX INX / CPX #0006 / BNE .cmp
CECDD    LDX $86 / STA $98FF,X              ;   not listed -> append at the cursor (cursor += 2)
CECE6  .bump: INC $80,X                     ; counts[X] (X = 0/2/4 -> $80/$82/$84)
CECEB  .next: INC $88 / INY INY / CPY #0010 / BNE loop        ; 8 slots
CECF4  $98CC = $80 ; $98CD = $82 ; $98CE = $84                ; the 3 per-type COUNTS
CED03  $80 = 0
CED07  .n: LDX $80 / LDA $9900,X / BMI ->CED7C                ; list entry unused (0xFFFF) -> skip its row
CED10     LDA $98FF,X ; A*11 -> X           ; (ASL ASL -> *4, +*4 -> *12, SEC SBC id -> *11)
CED20     copy $CC6500+X (11 bytes) -> $94A0..                ; the ROM name record
CED53     Y = $80 * 12                      ; $80 = 0,2,4 -> dest 0, 24, 48
CED60     copy 12 bytes $94A0 -> $ACBC,Y    ; 11 name codes + 0xFF
CED6F     copy 12 bytes $94B0 -> $ACBC,Y    ; the all-0xFF BLANK row (the tilemap row between two names)
CED7C  INC $80 INC $80 / CMP #06 / BNE .n
CED86  RTL
```

So `$7E:ACBC` is the strip's own **name text buffer**: three 24-byte slots (11 name codes + `FF`, then a
12-byte blank row), one per DISTINCT staged enemy type, rendered at box0 rows **2 / 4 / 6**.

## 2. Oracle proof

- **Written ONCE.** `--monitor "ACBC:12" --from forest_holdleft_pre.state --btnscript "40:600,0:200" --diff
  --writer`: `f229 = 00×12 (CF:FB9A clear)`, `f234` the alive list stages (`FD:B2B4`), **`f240 = A7 BE CD BA
  C4 BE EF EF EF EF EF FF` (writer `CC:ED66`)** — and nothing else, ever. The same monitor over a whole
  fight from `forest_battle_3hetake.state` logs ZERO writes in ~1000 frames.
- **It survives every death.** At the results screen (`--peek forest_battle_3hetake … "<58 A-taps>,0:150"`,
  f846) the capture's WRAM reads `$96F8 = 00 00 00 00 00 00 00 00`, `$AF02 = FF ×8`, and
  `$ACBC = A7 BE CD BA C4 BE EF EF EF EF EF FF` — and the rendered strip still says `Hetake`.
- **One row per DISTINCT type, at rows 2/4/6.** `bat2_lake` (`$29BF`: slots 1,2 = id 0x8D, slot 5 = id 0x19)
  reads `$98FF = 8D 00 19 00 FF FF`, `$98CC = 02 01 00`, and
  `$ACBC = "Beetle"+5×EF+FF | FF×12 | "Avian"+EF+"Chaos"+FF | FF×12`; the oracle's own strip renders
  **`Beetle`** on the first name row and **`Avian Chaos`** on the second.
- **`$96F8` == our staged-record validity.** bat2 `$96F8 = 00 01 01 00 00 01 00 00` and the staged records
  `$29BF+12k` have `rec[6] & 0x80` clear for exactly slots 1, 2, 5.
- **0xEF is the record's SPACE, not a terminator.** id 0x0C = `A1 C5 CE BE EF A8 C6 C9 EF EF EF` = "Blue
  Imp"; id 0x19 = `A0 CF C2 BA C7 EF A2 C1 BA C8 CC` = "Avian Chaos". Only the TRAILING run is padding.

## 3. Shipped

- `battleStageEnemyNames()` (`src/battle.c`, called at the end of `stageAll`) = §1 verbatim: dedupe the
  staged slots' ids into `$98FF` (3 entries, high byte 0xFF = unused) with per-type counts in `$98CC`, and
  write each used entry's ROM name record into `$7EACBC + k*24` (11 bytes + `FF`, then the 12-byte blank
  row). Gates: `rec[6] & 0x80` = EMPTY (== `$96F8`), `rec[7] & 0x80` = HIDDEN (== `$9FFA` via CC/EAB5).
- `CtBattleHudRender` phase 0 now blits `mem[0xACBC + k*24 + i]` for k = 0..2 at rows 2/4/6, gated on the
  ROM's own `LDA $9900,X / BMI` (high byte 0xFF = unused entry). The bytes ARE the font tiles, so 0xEF blits
  as the record's pad/space exactly as the tilemap does.
- `CtBattleEnemyName` (the ASCII helper the debug lines use) maps 0xEF to a space and trims only the
  trailing run — it used to stop at the first 0xEF, printing `Blue` and `Avian`.

Effect: the name survives the last kill (the report), a two-type formation shows BOTH names, and a name with
an embedded space is whole.

## 4. OPEN

- **A 4th distinct type.** The ROM appends at `$86` with no bound: a fourth type writes `$98FF+6` (past the
  3-entry list) and `INC $80,X` then increments `$86` itself. Our builder stops at 3 rather than reproduce
  an out-of-bounds write; no CT formation with 4 distinct enemy types was captured.
- **The 12th byte** of each name row reads `FF` in every capture; the disassembly's `TDC / STA $94AB` path
  says 0, so the FF's provenance (probably the `CC/F063` service between the copy and the store) is not
  pinned. Our builder writes FF because that is what the RAM reads; the strip renders only cells 1-11.
- (TRACED 2026-09-23: C1/04DC-0545, inside the strip rebuild C1/0299, rows 2/4/6 cols 1..11 of the text buffer $0CC0;
  the command windows go over it -- battle_hud_command_windows.md.) **Where the ROM re-renders `$ACBC` into the BG3 tilemap** each frame was not traced (it does not matter for
  the port: our HUD reads the buffer directly).
- **The strip's POSITION** (the oracle flips the whole strip to the TOP of the screen when the party stands
  low, e.g. `bat2_lake`) is a separate, still-unmodelled mechanism — our strip is always at renderY 160.
