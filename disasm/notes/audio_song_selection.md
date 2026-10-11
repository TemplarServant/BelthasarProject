# AUDIO — WHICH SONG PLAYS WHEN (the data side): DERIVED, oracle-proven

**Front 3 of 4 on the audio system.** Front 2 derives *how* a command is issued; this dossier derives
**what id is issued, by whom, and when** — the selection/trigger layer, so the port picks the canonical
track with zero invention. The Akao sequence format and the samples are explicitly **out of scope**
(the emulated SPC700 plays them for free once the same `$2140-$2143` bytes are sent).

**Companions:** `audio_commands.md` (Front 2 — the `$1E00` mailbox / `JSL $C7:0004` protocol, the 59-site
call catalogue) and `apu_upload.md` (the boot-time IPL upload). Fronts 2 and 3 were derived independently
and **agree byte-for-byte** on the overlap (`C0/1B53`, `$7E29AE`, `$1E05 = $E0` idle gate, `C0/1BAB`/`C0/1BE6`,
the `$FF` skip) — that agreement is itself a cross-check. What is new here: the record's stride/extent proof,
the 512-record range histogram, the loader-chain vs. menu-reload asymmetry, the whole **overworld** selection
path, the **callers** of the battle thunks, and live `--monitor` captures of the battle-in/battle-out cues.

Every claim below is either (a) a ROM address disassembled with `python tools/dis.py <flatoffset> <len> [m] [x]`
(ROM = `Chrono_Trigger_USA_SNES.sfc`, HiROM: flat offset == the bank-C0 CPU offset; bank $XX = flat
`(XX-0xC0)<<16 | addr`), or (b) an **observed oracle value** (`build/ct.exe --read / --monitor / --teleport`).
Observed values are marked ⟦OBS⟧ with the exact command.

---

## HEADLINE (the six facts the port needs)

1. **Field location song = byte +0 of the 14-byte location record at `$F6:0000` (flat `0x360000`).**
   Read at **`C0/1B79 LDA $F60000,X`**, X = `loc*14` (built at **`C0/092B`**, hardware multiply `LDA #0E / STA $4203`).
2. **The song shadow is `$7E29AE`** (written `C0/1B7F` on a field load, `C0/3C35` by event-VM op-`0xEA`).
   Battle-end and menu-return **replay the shadow**, they do not re-read the record.
3. **A record byte with bit 7 set (`$FF`, 87 of 512 locations) means "DO NOT CHANGE THE MUSIC"** —
   `C0/1B7D BMI ->1B8F` skips both the shadow store and the play. That, not a compare, is the
   field-side suppression. (A same-song *compare-and-skip* does exist, but on the **overworld**:
   `C2/3009 LDA $1B9B,X / CMP $02AE / BEQ`.)
4. **The issuer on scene load is `C0/1B53`, the 2nd step of the field loader chain `C0/00F4`**
   (`00F4 JSR C0/092B` → `00F7 JSR C0/1B53` → tileset/palette/map/script). The **menu-return reload
   `C0/01A5` deliberately starts at `C0/0960`, skipping `092B`+`1B53`** — that is why closing the menu
   does not restart the song.
5. **Battle**: start = `C0/1BAB` (entered as `JSL $C0:0008`, a BRL thunk, from **`D1/F405`**) → plays
   **`$01FA` = `$45`**, a *constant* poked at field init (`C0/0C58 LDA #45` / `C0/0C5A STA $FA`, D=$0100), with
   **command `$14`** — unless **`$7E2A1F` bit 6** is set, in which case it sends command `$70`/param 1
   ("keep the field music, swap the driver mode") instead. End = fade `CD/039E` (skipped when the same
   bit 6 is set, `CD/03CC`), then **`C0/1BE6`** (`JSL $C0:000B` from **`CD/043E`**) replays **`$7E29AE`
   with command `$11`**. Game-over plays **song `$26`** (`C0/1CAF`, cmd `$14`); the battle-end jingle is
   **song `$3A`** (`CD/0D7E`, cmd `$10`, preceded by cmd `$80`).
6. **`$1E01` is not one id space.** The *command* is `$1E00`; `$1E01` is its argument. For commands
   `$10-$17` it is a **song id and must be `< $53`** (`C7/01A5 CMP $C70AE9`, and `rom[$C7:0AE9] = $53`).
   For `$18-$2F` it is a **sound-effect id** (the ROM uses `$53`, `$65`, `$6F`, `$95`, `$FF`, … there).
   For `$80-$FF` (fade/volume/tempo) it is a **parameter**. All 512 location records hold either a value
   `<= $46` or `$FF` ⟦OBS ROM histogram⟧ — consistent with the `< $53` range check and the `BMI` skip.

---

# A — THE SOUND-COMMAND MAILBOX `$1E00-$1E05` AND ITS DISPATCHER

## A.1 The mailbox
Every sound request in the game is written into the fixed WRAM block and then dispatched by
`JSL $C7:0004` (= `JMP $C7:0140`, entry table `C7/0000-000F`, see `apu_upload.md`):

| cell | role |
|---|---|
| `$1E00` | **command** (cleared to 0 by the dispatcher when it finishes, `C7/0194 STZ $00`) |
| `$1E01` | argument: song id / SFX id / fade parameter |
| `$1E02`, `$1E03` | extra parameters; for a **song** command they are **overwritten** by `C7/0A45-0A51` |
| `$1E05` | **driver-busy mutex**, idle value **`$E0`** |
| `$1E10`/`$1E11` | driver *mode* (0/1), set by command `$70` |

`tools/dis.py 0x70140 0x120` — dispatcher (D=`$1E00`, DB=`$00`):
```
70157  LDA $05 / BPL ->70192      ; $1E05 < $80 (a load in progress) -> DROP the command
7015B  LDA $00 / BEQ ->70192      ; no command
7015F  BMI ->7017A                ; $80-$FF  -> C7/061C (raw pass-through)
70161  CMP #10 / BCC ->70192      ; $01-$0F  -> ignored
70165  CMP #18 / BCS ->70176
70169  AND #0F / ASL / ... JMP ($0AD9,X)   ; $10-$17 -> jump table
70176  CMP #30 / BCS ->7017D ; else JMP C7/061C          ; $18-$2F = SOUND EFFECTS
7017D  CMP #40 / BCS ->70184 ; else JMP C7/071D          ; $30-$3F
70184  CMP #70 / BEQ  JMP C7/08E3                        ; $70 = driver mode toggle
7018B  CMP #71 / BEQ  JMP C7/0755 ; else ->70192
```
Jump table `rom[$C7:0AD9..0AE8]` = `A1 01 A1 01 92 01 92 01 A1 01 A1 01 92 01 92 01`:
**`$10,$11,$14,$15 → C7/01A1` (play song); `$12,$13,$16,$17 → C7/0192` (the exit stub = no-op).**

## A.2 The song handler `C7/01A1` and the `< $53` range check
```
701A1 SEP #20
701A3 LDA $01 / CMP $C70AE9 / BCS ->70192   ; rom[C7/0AE9] = $53  -> songs are $00..$52
701AB JSR C7/0734                            ; (inert in this ROM: the scan list at C7/0AD8 is a lone $FF)
701AE JSR C7/0A39                            ; -> fills $1E02/$1E03 and SENDS $1E00-$1E03 to $2140-$2143
701B1 LDA $01 / STA $05                      ; $1E05 := song id  == the BUSY mutex (bit7 clear -> drops)
701B5 ...                                    ; instrument-set diff/load ($1F00/$1F20 lists, table C7/0E11)
             ... ends restoring $1E05 = $E0 (C7/0601: LDA #E0 / STA $2140 / ... / STA $05)
```
`C7/0A39` (the port write) also derives the song's ARAM parameters from two ROM tables:
```
70A3C LDA $01 (16-bit) / AND #007F / ASL / TAX
70A45 LDA $C7241E,X / STA $02          ; $1E02 = rom[$C7:241E + song*2]
70A4B LDA $C7241D,X / AND #0F / STA $03; $1E03 = rom[$C7:241D + song*2] & $0F
70A5A STA $2143 / $2142 / $2141 / $2140 ; then busy-wait for the $2140 echo (retry loop)
```
⟦OBS⟧ verified for four songs: `$06 → (C8,05)`, `$1E → (E8,0D)`, `$08 → (B0,04)`, `$45 → (B0,04)` —
each matches the live `$1E02/$1E03` after the corresponding play
(`ct.exe --read "wram:001E00+8" --from …`).

⟦OBS⟧ the **`$1E05` mutex** in flight:
`ct.exe --monitor "1E00:8,…" --from saves/battle_re/forest_holdleft_pre.state --btnscript "40:180,…" --diff --writer`
→ `f173  001E00=1445B00400`**`45`**`0000 (C7:01B5)` (mid-load, `$1E05=$45`), `f229 …E0…` (idle again).

## A.3 What the other commands are (used by the field/battle code)
| cmd | handler | observed use |
|---|---|---|
| `$10` | C7/01A1 | play song — field load `C0/1B88`, VM op-`0xEA` `C0/3C3E`, overworld `C2/2F3C`, jingle `CD/0D79` |
| `$11` | C7/01A1 | play song (resume variant) — battle-end restore `C0/1C37`, menu restore `C2/CC3B` |
| `$14` | C7/01A1 | play song — battle start `C0/1BCB`, game-over `C0/1CB4`, menu song `C2/CBD0` |
| `$18` | C7/061C | **sound effect** — VM op-`0xE8` `C0/3C23`, battle `CD/3B76`, `C1/40DA`, `C2/6BDC` |
| `$19` | C7/061C | sound effect variant — `C0/1B9F` (`$01FC=$53`) / `C0/1BA7` (`$01FB=$65`) |
| `$70` | C7/08E3 | **driver mode** 0/1; **idempotent** (`708ED CMP $11 / BEQ ->7091F`); battle `C0/1BDC`/`C0/1C86` |
| `$80` | C7/061C | clear/stop before the jingle — `CD/0D65` (`$1E01=0`, `$1E02=FF`) |
| `$81` | C7/061C | **fade / volume** — `C0/1B70` (`00,FF,FF` before a location song), `C0/1C27` (`00,00,FF`), `C0/1C4F` (`40,FF,FF`), `C0/3C64` (op-`0xEB`), `CD/03A0` (`10,00` = the battle fade-out) |
| `$82`,`$83` | C7/061C | the two commands that always follow `$81` in the op-`0xEB` / battle-restore triple |

`C7/061C` is a **verbatim pass-through**: `$1E03→$2143, $1E02→$2142, $1E01→$2141, $1E00→$2140` + echo wait.

---

# B — FIELD LOCATIONS: the record byte, the shadow, the skip

## B.1 The record — offset, stride, extent
* Table base **`$F6:0000` = flat `0x360000`** (`src/scene.c:19 #define SCENE_HDR 0x360000`, and the ROM's own
  `C0/1B79 LDA $F60000,X` / `C0/0962 LDA $F60001,X` / `C0/6DD1 …0002` / `C0/7086 …0003` / `C0/A33F …0004`
  / `C0/56D8 …0008` / `C0/747A-748E …000A-000D`).
* **Stride = 14 (`$0E`)**, from `C0/092B`:
  ```
  092B LDA $01 / AND #01 / BNE ->0945          ; loc >= $100 ?
  0931 LDA $00 / STA $4202 / LDA #0E / STA $4203 / REP #20 / LDA $4216
  0945 (loc>=$100) same, then CLC / ADC #0E00  ; + 256*14
  095B STA $FE                                 ; $01FE = loc * 14
  ```
  512 records × 14 = `0x1C00` → the table spans `0x360000-0x361BFF`, exactly abutting
  `SCENE_TS 0x361C00` (`src/scene.c:20`) — an independent extent check.
* **Byte +0 = the MUSIC id.** Range from the data ⟦OBS ROM histogram over all 512 records⟧:
  every value is either `<= $46` **or** `$FF`; `$FF` occurs **87** times. No value lies in `$47..$FE`.
  This matches the driver's `< $53` range check (A.2) and the `BMI` skip (B.3) exactly.
* The rest of the record (already used by `src/scene.c:506`): +1 tileset L12, +2 tileset L3, +3 palette,
  +4/+5 map, +8/+9 event script, +10..+13 scroll mask. `$FF` is the generic "none" sentinel here too
  (`C0/0966 CMP #FF / BNE ->096B / RTS` for the tileset).

## B.2 The issuer `C0/1B53` — the ONLY field music trigger on a scene load
`tools/dis.py 0x1B53 0x40`:
```
1B53  LDA $7F01ED
1B57  BEQ ->1B5F
1B59  LDA $7E29AE           ; $7F01ED != 0  -> replay the SHADOW (skip the fade + the record)
1B5D  BRA ->1B83
1B5F  LDA #00 / STA $1E01
1B61  LDA #FF / STA $1E02
1B69  LDA #FF / STA $1E03
1B6E  LDA #81 / STA $1E00 / JSL C70004      ; FADE the current song out
1B77  LDX $FE                               ; = loc*14
1B79  LDA $F60000,X                         ; ★ the location's MUSIC BYTE
1B7D  BMI ->1B8F                            ; ★ bit7 set ($FF) -> DO NOT CHANGE THE MUSIC
1B7F  STA $7E29AE                           ; ★ commit the SHADOW
1B83  STA $1E01
1B86  LDA #10 / STA $1E00 / JSL C70004      ; play
1B8F  RTS
```
Call chain (`tools/dis.py 0x66`, `0xF4`; byte-search `20 53 1B` → **exactly one** caller, `C0/00F7`):
```
C0/0066  LDX $0100 / CPX #01F0 / BMI ->0072 / JML C20000   ; loc >= $1F0 = OVERWORLD, leaves bank C0
0072     CPX #01FF / BMI ->007D
007D     D=$0100 ; 0085 JSR C0/0B86 ; 0088 JSR C0/00F4 ; 008B JSR C0/B192 ...
C0/00F4  JSR C0/092B   (loc*14)
00F7     JSR C0/1B53   ★ MUSIC
00FA     JSR C0/0960 (ts12) / 6DCF (ts3) / 7084 (pal) / 7F7E / A33B (map) / 09DD / 0A14 / 56D4 (script) ...
```
**`C0/01A5`, the post-menu reload chain, begins at `JSR C0/0960`** (`tools/dis.py 0x1A5`) — it contains
neither `092B` nor `1B53`, which is the ROM's own reason the menu never restarts the song.

**`loc >= $1F0` never reaches `C0/1B53`** (the `JML C2/0000` at `C0/006E`) ⇒ **byte +0 of the overworld
records `$1F0-$1FF` is DEAD DATA.** Proof from the data itself: record `$1F7` byte0 = `$00` while the
live world-7 region table `$1B9B[0]` = `$05` ⟦OBS⟧ (§D).

## B.3 `$7F01ED` — the "replay the shadow instead of the record" gate
`C0/1B53`'s first branch, and the identical gate at the overworld (`C2/2F0F`, `C2/2F4E`, `C2/2FB4`).
⟦OBS⟧ `$7F01ED = 0` in every ordinary field/overworld state examined
(`truce_market.state`, all teleported states, `ow_w0.state`), so the normal path is the record path.
The flag's writers were not traced (§J.3).

## B.4 The concrete songs — ROM record byte vs. the live shadow ⟦OBS⟧
Method: `ct.exe --teleport <loc> 8 8 0 --from saves/truce_market.state --frames 180 --out tp.state`
then `ct.exe --read "loc,wram:7E29AE+1,wram:001E00+8" --from tp.state`.
(`truce_market.state` is loc `$011`, shadow `$1E` at f0 — the inherited value that proves the `$FF` skip.)

| loc | record 14 bytes (`0x360000 + loc*14`) | byte+0 | live `$7E29AE` after load | verdict |
|---|---|---|---|---|
| 2 (Crono's bedroom) | `FF 02 01 02 3C 00 00 00 44 01 10 01 1F 0E` | **`$FF`** | `$1E` (inherited) | **skip** — see B.5 |
| 5 (Leene's Sq. fair) | `06 00 00 00 4D 00 00 00 1B 00 00 13 2F 2F` | `$06` | **`$06`** | ✔ |
| 6 (Leene's Sq. fair) | `06 00 FF 00 4F 00 00 00 6F 00 00 00 0F 0F` | `$06` | **`$06`** | ✔ |
| 8 (Telepod square) | `06 01 0E 01 47 00 00 00 1C 00 00 00 0F 0D` | `$06` | **`$06`** | ✔ |
| 12 (Truce inn) | `1E 02 01 02 3C 00 00 00 45 01 30 00 3F 1E` | `$1E` | **`$1E`** | ✔ |
| 18 (Truce, outside) | `1E 02 01 02 3C 00 00 00 4B 01 00 32 0F 3F` | `$1E` | **`$1E`** | ✔ |
| 19 (`$13` Guardia Forest) | — | `$08` | **`$08`** ⟦monitor f0⟧ | ✔ |
| 120 (`$78`) | `0C 04 FF 05 42 00 00 00 F0 00 00 02 3F 38` | `$0C` | **`$0C`** (`$1E02/03 = B0/08`) | ✔ |
| 271 (`$10F`, wake-up) | `FF FF FF 4B 1F 00 00 00 69 01 00 00 3F 3F` | **`$FF`** | `$1E` (inherited) | **skip** |
| 439 (`$1B7` upper plaza) | `06 00 00 00 4E 00 00 00 4A 00 00 00 2F 16` | `$06` | **`$06`** | ✔ |
| 496-500 (`$1F0-$1F4`) | `01/02/05/04/03 12 FF 4B 00 00 00 00 18 00 80 00 00 00` | — | **not read** (§D) | dead byte |

**The `$FF` skip, caught mid-flight** ⟦OBS⟧: `--teleport 2 … --frames 20|40|60|90`:
```
frames=20  $1E00..03 = 00 00 FF FF   <- only the C0/1B5F..1B73 cmd-$81 fade ran; the play was skipped
frames=40  $1E00..03 = 00 00 FF FF
frames=60  $1E00..03 = 00 1E E8 0D   <- a song-$1E play appeared BETWEEN f40 and f60
```
Same signature for loc 271 at frames=180: `00 00 FF FF`. So an `$FF` record still issues the **fade**
(`cmd $81, 00/FF/FF`) and then plays nothing.

## B.5 Where an `$FF` location's music actually comes from: the event script (op-`0xEA`)
⟦OBS⟧ `ct.exe --monitor "1E00:6,7E29AE:1" --from tp2_40.state --frames 40 --diff --writer`:
```
f0   001E00=0000FFFF00E0   7E29AE=1E
f3   001E00=001EE80D00E0   7E29AE=1E (C0:3C39)
```
The **last writer of `$7E29AE` is `C0/3C39`** = the instruction after `C0/3C35 STA $7E29AE`, i.e.
**event-VM op `0xEA`** (§C). The scene-2 script image in `$7F2000` contains the byte pair `EA 1E`
⟦OBS `--read "wram:7F2000+2048"`, offset `0x372`⟧. So the canonical pattern is:
**`$FF` = "the script owns the music here"** (or it is inherited from the previous location).

---

# C — THE EVENT VM's SOUND OPCODES (bank C0)

Op-dispatch table base **`C0/5D6E`** (entry = `base + op*2`; anchored on the three ops already in the
memory index: op `$DD`→`C0/3B1B` @`5F28`, op `$E0`→`C0/3AB7` @`5F2E`, op `$E3`→`C0/3C05` @`5F34`).

| op | handler | length | semantics (disassembled `tools/dis.py 0x3C11 0xC0`) |
|---|---|---|---|
| `$E8 nn` | `C0/3C11` | 2 | **SOUND EFFECT**: `$1E01=nn`, `$1E02=$80`, cmd `$18` |
| `$E9` | `C0/5F6E` | 1 | unused stub |
| **`$EA nn`** | **`C0/3C2F`** | **2** | **PLAY SONG**: **`$7E29AE = nn`** (`3C35`), `$1E01=nn`, cmd `$10` |
| `$EB aa bb` | `C0/3C4C` | 3 | `$1E01=aa,$1E02=bb,$1E03=FF` cmd `$81`; then `(00,FF)` cmd `$82`; then `(00,FF)` cmd `$83` — the **fade/volume triple** |
| `$EC c0 c1 c2` | `C0/3C98` | 4 | **RAW command**: `$1E00=c0, $1E01=c1, $1E02=c2`, dispatch |
| `$ED` | `C0/3CBC` | 1 | **WAIT** until `$2141 == 0` (re-executes the op while non-zero: `TYX / LDA $2141 / BNE ->3CC3 / INX`) |
| `$EE` | `C0/3CC5` | 1 | **WAIT** until `($2143 & $0F) == 0` |

The port's `src/ct_vm.c` `OPLEN` row `0xE0-0xEF` is `5,5,5,2,8,8,5,3,`**`2,1,2,3,4,1,1,1`** — i.e.
`E8=2, EA=2, EB=3, EC=4, ED=1, EE=1`: **already byte-correct**, the ops are simply parsed and dropped
(no audio handler exists anywhere in `src/`). `ct_vm.c:1190` even documents that op `$E8` yields the
visit — "(audio)" — which is the ROM's `CLC/RTS` tail.

---

# D — OVERWORLD (`loc >= $1F0`): a per-region table + the real compare-and-skip

The overworld leg never runs `C0/1B53` (§B.2). Its music comes from a **16-entry RAM table `$7E1B9B`**
indexed by a **per-tile nibble map**:

`C2/62ED` (`tools/dis.py 0x262ED 0x40`) — returns the region nibble for map cell (`$00`,`$01`):
```
262EF LDA #7E / STA $12
262F3 LDA $01 / STA $4202 / LDA #30 / STA $4203       ; row * 0x30
26300 LDA #7200 / ADC $4216 / STA $10                 ; $7E7200 + row*0x30
26308 LDA $00 / AND #00FF / LSR / TAY
26311 LDA [$10],Y                                     ; one byte = two columns
26313 LSR $00 / BCS ->2631C ; LSR x4 / RTS  |  2631C AND #0F / RTS
```

**Entry issuer `C2/2F0F`** (`tools/dis.py 0x22F00 0x60`):
```
22F0F LDA $7F01ED / BNE ->22F30
22F15 LDA $0102 / LSR / STA $00     ; arrival cell / 2
22F1B LDA $0103 / LSR / STA $01
22F21 JSR C2/62ED / TAX
22F25 LDA $1B9B,X / STA $1E01 / STA $02AE      ; ★ region -> song, and the OW shadow $02AE
22F30 (else) LDA $7E29AE / STA $1E01 / STA $02AE
22F3A LDA #10 / STA $1E00 ; $1E02=FF ; $1E03=80 ; JSL C70004
```

**Walk-time re-evaluation + THE COMPARE-AND-SKIP** — `C2/2FF0..3035`:
```
23001 JSR C2/62ED / STA $EB / TAX
23009 LDA $1B9B,X
2300C CMP $02AE                      ; ★ same song as the one playing?
2300F BEQ ->23034                    ; ★ YES -> do nothing (no restart)
23011 ... $1BE8=$81, $1BE9=$0C, $1BEA=0, $1BEB=FF, $1BED=0 ; JSR C2/2ED9   ; else start the fade-out
```
and `C2/2F4E` (called from `C2/45A6`) stages the *new* song the same way into the deferred block
`$1BE8-$1BEB` and commits it through the priority gate **`C2/2ED9`**
(`LDA $1BEC / BEQ issue / BMI skip / LDA $1BEE / CMP $1BED / BCC skip`), copying `$1BE9 → $02AE`.

**`$02AE` is the overworld's current-song cell** (distinct from `$7E29AE`); the menu syncs it from the
field shadow at `C2/2C50 LDA $7E29AE / STA $02AE`.

⟦OBS⟧ live per-world tables (`ct.exe --read "loc,wram:7E1B9B+16,wram:7E02AE+1" --from saves/ow_w*.…`):

| state | loc | `$1B9B[0..15]` | `$02AE` |
|---|---|---|---|
| `ow_w0.state` | `$1F0` (world 0, 1000 AD) | `01 1E 3C 00×7 03 00×5` | `$1E` |
| `ow_w1.live` | `$1F1` (world 1) | `02 2D 2D 00×7 03 00×5` | `$02` |
| `ow_w3.live` | `$1F3` (world 3) | `04 00×9 03 00×5` | `$04` |
| `ow_w7.live` | `$1F7` (world 7) | `05 00×9 03 00×5` | `$05` |

So region 0 = the world theme, region 10 = `$03` on every world, and 1000 AD carries `$1E` (the
Truce/village theme, matching loc 12/17/18) and `$3C` on two of its regions.
⟦OBS⟧ the table **persists in `$7E1B9B` across a field↔overworld hop** (identical in
`truce_market.state` and `ow_w0.state`; `--monitor "7E1B9B:16,…" --btnscript "20:600"` shows the
`loc → $1F0` transition at f33 with **no write** to it) — its loader was not traced (§J.1).

---

# E — BATTLE

## E.1 Battle start — `C0/1BAB`, reached as `JSL $C0:0008`
The bank-C0 vector page (`rom[0x0000..0x000E]` = `80 5B | 82 3C 2C | 82 F7 0A | 82 A0 1B | 82 D8 1B`) is
five branch thunks:
`C0/0000 → 005D`, `C0/0002 → 2C41`, `C0/0005 → 0AFF`, **`C0/0008 → 1BAB`**, **`C0/000B → 1BE6`**.
Byte-search `22 08 00 C0` → **`D1/F405`** (the battle boot, right after `JSL CE/EE6E`, `JSL D1/F83D`,
`JSL CD/0235`). Byte-search `22 0B 00 C0` → **`CD/043E`** (the battle teardown).

```
C0/1BAB  PHB/PHD ; D=$0100 ; DB=$01
1BB7     LDA $7E2A1F / BIT #40 / BNE ->1BD5     ; ★ bit6 = "battle keeps the field music"
1BBF     LDA #FF / STA $1E10
1BC4     LDA $FA                                 ; ★ $01FA = the BATTLE SONG id
1BC6     STA $1E01
1BC9     LDA #14 / STA $1E00 / JSL C70004        ; cmd $14
1BD5     (bit6 set) LDA #01 / STA $1E01 ; LDA #70 / STA $1E00 / JSL C70004
```
**`$01FA` is a constant**, poked once by the field init at `C0/0C50-0C5B` (`tools/dis.py 0xC48 0x20`)
(`0C50 LDA #53 / 0C52 STA $FC ; 0C54 LDA #65 / 0C56 STA $FB ; 0C58 LDA #45 / 0C5A STA $FA`, D=`$0100`):
`$01FA = $45` (the battle song), `$01FB = $65`, `$01FC = $53` (two SFX ids used by `C0/1B90`/`C0/1BA7`
with cmd `$19`). ⟦OBS⟧ `$7E01FA = $45` in every state read (`truce_market`, `base_loc6`,
`gato_inbattle`, `battle592`).

**⟦OBS⟧ the battle cue, captured live** —
`ct.exe --monitor "1E00:8,7E29AE:1,7E01FA:1,7E0100:2" --from saves/battle_re/forest_holdleft_pre.state --btnscript "40:220" --frames 220 --diff --writer`:
```
f0     001E00=0008B00400E00000   7E29AE=08  7E01FA=45  loc=$0013     ; field song $08 (Guardia Forest)
f21    001E00=006F800400E00000   (C7:0196)                            ; SFX $6F = the encounter
f173   001E00=1445B00400450000   (C7:01B5)                            ; ★ cmd $14, song $45, mutex $1E05=$45
```
`$1E02/$1E03 = B0/04` = exactly `rom[$C7:241E+$45*2] / rom[$C7:241D+$45*2]&$0F` ⇒ the id really is `$45`.

## E.2 Battle end — fade, then replay the shadow
```
CD/03B4  LDA $2A21 / BPL ->03C1 ; LDA $2A1F / AND #BF / STA $2A1F   ; $2A21 bit7 clears the keep-music bit
CD/03CC  LDA $2A1F / AND #40 / BNE ->03D6        ; ★ bit6 set -> SKIP the fade
CD/03D3  JSR CD/039E                              ; else fade out
CD/039E  LDA #81 / STA $1E00 ; LDA #10 / STA $1E01 ; STZ $1E02 ; JSL C70004 ; LDA #0F / JMP CD/3E3B
```
then, after the teardown, `CD/0438 STZ $2A21` and `CD/043E JSL $C0:000B` →
```
C0/1BE6  D=$0100, DB=$01
1BF2     LDA $7F01EC                       ; battle outcome (0 normal, 1 defeat, 2 …)
1BF6     BEQ ->1C09
1BF8     DEC A / BEQ ->1BFE                ; outcome 1:
1BFE     LDA $7E2A1F / BIT #20 / BNE ->1C09;   bit5 (scripted-loss ok) -> normal restore
1C06     BRL ->1C90                        ;   else GAME OVER (E.3)
1C09     LDA $7E2A1F / BIT #40 / BNE ->1C7F; ★ bit6 -> $1E01=0, cmd $70 (leave battle mode, music untouched)
1C11     $1E10=0 ; cmd $81 (00,00,FF)
1C2E     LDA $7E29AE / STA $1E01 ; LDA #11 / STA $1E00 / JSL C70004   ; ★ REPLAY THE FIELD SHADOW
1C3E     cmd $81 (40,FF,FF) ; 1C56 cmd $82 (00,FF) ; 1C69 cmd $83 (00,FF)
```
**⟦OBS⟧ the same forest run, driven to the battle's end** (`--btnscript "40:180," + "100:16,0:16"×40`,
`--frames 1600`):
```
f1370  001E00=0010000400E00000  (C7:0196)   ; CD/039E fade-out ($81 / 10 / 00)
f1406  001E00=1108B00400080000  (C7:01B5)   ; ★ cmd $11, song $08 = $7E29AE restored, mutex $1E05=$08
f1449  001E00=8140FFFF00E00000  (C0:1C52)   ; ★ the C0/1C3E-1C52 cmd-$81 tail — C0/1BE6 confirmed live
```
`$7E29AE` held `$08` unchanged for the whole battle ⟦OBS⟧ — the battle module does not clobber it.

## E.3 Game over — song `$26`
`C0/1C90` (reached from `C0/1C06`): `$1E10=0`; cmd `$81 (00,00,FF)`; **`LDA #26 / STA $1E01` + cmd `$14`**
(`C0/1CAF/1CB4`); then cmd `$81 (80,FF,FF)`, `$82 (00,FF)`, `$83 (00,FF)`.
(`C0/0283`'s own game-over branch separately runs the brightness ramp `$19` + `JSL FD/FFF7`.)

## E.4 The battle-end jingle — song `$3A`
```
CD/0D62  PHX
0D63     LDA #80 / STA $1E00 ; STZ $1E01 ; LDA #FF / STA $1E02 ; JSL C70004   ; clear
0D74     JSR CD/3E7D
0D77     LDA #10 / STA $1E00 ; LDA #3A / STA $1E01 ; JSL C70004               ; ★ SONG $3A
0D85     PLX ; JSR CD/0EBD ; STZ $5D9B ; JSR CD/0D33 ; INC $5D9B ; RTS
```
Reached only from `CD/0DA5` (inside `CD/0D93`, `X=$0087`) and `CD/0DD5` (inside `CD/0DB1`, `X=$0084`),
which are selected by:
```
CD/03EE  LDA $2A21 / AND #11 / BEQ ->040D        ; ★ neither bit0 nor bit4 -> NO jingle at all
03F5     AND #01 / BEQ ->0404
03F9     LDA $7F01EC / BNE ->040A ; JSR CD/0DB1  ; bit0 & outcome==0
0404     LDA $7F01EC / BEQ ->040D ; 040A JSR CD/0D93
```
⟦OBS⟧ In the Guardia-Forest fight above **`$7E2A21` was `0` for the entire battle** (monitored with
`--diff`; the only write was `CD:043B` = the `STZ`), so neither jingle path ran and the observed end was
exactly *fade → `cmd $11` replay*. The jingle id and its code path are ROM-derived; **the condition that
sets `$2A21` bits 0/4 is NOT derived** (§J.2).

## E.5 `$7E2A1F` bit 6 — the one battle-music switch the port must honour
Same bit, three sites, all consistent: **start** `C0/1BB7` (cmd `$70`/1 instead of the battle song),
**end fade** `CD/03CC` (skip it), **restore** `C0/1C09` (cmd `$70`/0 instead of replaying the shadow).
`$2A1F` is the **op-`0xD8` operand 2** staged by the field (`battle_entry.md` §26); the observed operand
patterns across scenes include `10 40` and `92 C0`, i.e. bit 6 does occur in real data.
Command `$70` itself is idempotent in the driver (`C7/08ED CMP $11 / BEQ`).

---

# F — MENU / other non-field song requests

* **`C2/CB90`** (bank-C2 menu): `LDA $02AE` (or `LDA $29AE` when `$0D32 != 0`) `→ STA $0F0E` (save the
  current song), **`CMP #3E / BEQ ->2CBBE`** (★ another same-song compare-and-skip), then
  `C2/CBCE-CBF3` **`cmd $14, song $3E`**; the restore is `C2/CC36 LDA $0F0E / STA $1E01` + `C2/CC3B cmd $11`.
* **`C3/09A4`** — a *deferred* song request with its own compare-and-skip:
  `LDA $0388 / BNE skip ; LDA $0384 / BIT #20 / BNE skip ; LDA $038A / CMP $0389 / BEQ skip ;
   STA $0389 / INC $0388 / cmd $10 with $1E01=$038A, $1E02=FF / JSL C70004 / STZ $0388`.
  ⟦OBS⟧ `$0388/$0389/$038A = 00/30/30` in every field and overworld state read ⇒ this subsystem is inert
  in ordinary play (bank C3 = intro/ending/credits territory); not the field or battle path.
* `FF/FEFF` (`cmd $10`, `$1E01 = $0F0A`) and `C2/C02A`/`C2/E696` (`cmd $F3`) exist but were not traced.

**Correction to an older note:** `battle_boot.md` line 9 and `battle_menu_synthesis.md` §1.2 label the
`JSL C0/2C41` at `C0/1895` a *"music cue"*. It is **not**: `tools/dis.py 0x2C41 0x80` shows a
`$7F3415`-gated integrator over `$7F3417/3418` into the `$7F341D/341E` accumulators and `$7F346B`
(`$7E2C74`) — a scroll/parallax accumulator tick, no `$1E00` write anywhere. The real battle-music
thunk one slot earlier in the same vector page is `C0/0008 → C0/1BAB`.

---

# G — THE ID SPACE (what is and is not a track)

* **Songs: `$00 .. $52`.** `rom[$C7:0AE9] = $53` is the count and the driver's hard range check
  (`C7/01A5 CMP $C70AE9 / BCS -> exit`). Two parallel 83-entry tables give each song its ARAM
  parameters: `rom[$C7:241D + id*2]` (low nibble → `$1E03`) and `rom[$C7:241E + id*2]` (→ `$1E02`).
* **Not a track — the record encoding:** a location byte with **bit 7 set** (`$FF` in this ROM) =
  "keep the current music". `C0/1B7D BMI`. 87 of 512 records.
* **Not a track — the argument of a non-song command:** `$1E01` under cmd `$18-$2F` is a **SFX id**
  (`$53`, `$65`, `$6F`, `$95`, `$42`, `$4F`, `$FF`, … all observed live in the forest fight); under
  `$80-$FF` it is a **fade/volume/tempo parameter** (`$81` triples like `(00,FF,FF)`, `(40,FF,FF)`,
  `(80,FF,FF)`, `(10,00)`); under `$70` it is a **mode flag 0/1**.
* **Ids the ROM names explicitly:** `$45` battle, `$26` game over, `$3A` battle-end jingle,
  `$3E` the C2-menu song, `$0C`/`$81` the overworld fade-out request pair.

---

# H — PORT SPEC (what to implement; nothing applied — derive lane)

**H.1 State.** One byte `songShadow` mirroring `$7E29AE` (persist it across scene loads and across the
battle, like the ROM's WRAM), plus, for the overworld, `owSong` mirroring `$02AE`, and `sndBusy`
mirroring the `$1E05` mutex if the port ever needs to drop a command mid-load.

**H.2 Field scene load.** In the port's at-black transition (`src/field.c` ~5160-5185, `fieldLoad(...)`
for `pendKind==1|3`, and the initial load) run the `C0/1B53` law **in this order**:
1. issue `cmd $81 (00,FF,FF)` (the fade);
2. `m = CtAssetGet(a, CT_ASSET_SCENE_HEADER, loc, &n)[0]` — the packer already emits all 14 bytes
   (`assets/extract_assets.py:68` `rom[SCENE_HDR + s*14 : +14]`, `src/assets.h:14`);
3. **if `m & 0x80` → stop** (no shadow write, no play);
4. else `songShadow = m`; issue `cmd $10` with `$1E01 = m`.
   Do **not** run this on the menu-return reload (mirror `C0/01A5`), and **not** for `loc >= 0x1F0`.

**H.3 Event VM.** Add handlers for the six ops in §C (lengths are already correct in `OPLEN`):
`0xEA` must write `songShadow` **and** play; `0xE8` is SFX; `0xEB`/`0xEC` are raw command emitters;
`0xED`/`0xEE` are APU-port waits (in a port with a synchronous driver they are no-ops that must still
consume zero bytes and re-run/advance exactly as the ROM does).

**H.4 Overworld.** On overworld entry play `$1B9B[region(arrivalCell)]`; on every walk step
re-evaluate and **skip when equal to `owSong`** (`C2/3009`). The nibble map lives at `$7E7200`,
row stride `0x30`, 2 columns per byte, low nibble for even columns (`C2/62ED`). Until `$1B9B`'s loader
is derived (§J.1), the four measured tables above are the only canonical data.

**H.5 Battle.** Start: if `$7E2A1F & 0x40` → `cmd $70`/1, else `cmd $14` with id `0x45`.
End: if `!($2A1F & 0x40)` → `cmd $81 (10,00)` fade; then if `$2A1F & 0x40` → `cmd $70`/0 else
`cmd $11` with `songShadow`. Defeat without `$2A1F` bit 5 → `cmd $14` with `0x26`.
Jingle `0x3A` only when `$7E2A21 & 0x11` selects it (§J.2 — leave it unimplemented rather than guess).

**H.6 Do not invent a "same song → skip" on the field path.** The ROM has none; the suppression is the
`$FF` record byte. Adding a compare would silently change behaviour for the 425 non-`$FF` locations.

---

# J — OPEN ITEMS (NOT DERIVED)

1. **`$7E1B9B` loader.** No indexed store to `$1B9B` exists in the ROM (scan of `AD/8D/BD/9D/B9/99/9E/9C`
   with operand `1B9B` → only the four `LDA $1B9B,X` readers), and the 1000 AD table bytes
   `01 1E 3C 00…03` are **not** present contiguously in the ROM ⇒ it is written by a block copy /
   decompressed world header. **Closes it:** `--monitor "7E1B9B:16" --writer` across a *world change*
   (e.g. a Gate/Epoch hop to a different era, or `--teleport 0x1F1` from a 1000 AD state), then
   disassemble the reported writer PC.
2. **`$7E2A21` bits 0/4** (the battle-end jingle gate). Only reads and one `STZ` (`CD/0438`) are findable;
   the setter uses an addressing form the operand scan misses. Observed `0` through a full Guardia-Forest
   battle, so song `$3A` never fired there. **Closes it:** `--monitor "7E2A21:1" --writer` over a battle
   that *does* show the results/jingle (a boss, or a fight that awards items), then disassemble the PC.
3. **`$7F01ED`** ("replay the shadow instead of the record", `C0/1B57`, `C2/2F13`, `C2/2F52`, `C2/2FB2`).
   Observed `0` in every state examined; its writers were not traced. **Closes it:**
   `--monitor "7F01ED:1" --writer` across a menu open/close and a battle round-trip.
4. **`$01FA` is a constant `$45` in this ROM** — no other writer was found in banks C0-C2 (`85 FA` scan)
   and `F0/E2A1 STA $01FA` was not followed. If boss music is ever observed to differ from `$45` *without*
   the `$2A1F` bit-6 route, that site is the place to look. **Closes it:** `--monitor "7E01FA:1" --writer`
   into a boss fight.
5. **Song → name mapping.** Deliberately not attempted: nothing in the ROM maps ids to titles, and
   guessing would be invention. The ids and their triggers above are sufficient for the port.
6. **`C7`-side detail** deliberately left at the boundary: `C7/0734`'s scan list is a lone `$FF`
   (`rom[$C7:0AD8] = $FF`) so the routine is inert; the `$1F00/$1F20` instrument-set diff and the
   `C7/0E11` table (`song*32`) are driver/ARAM concerns, not selection.

---

## Repro commands (all short, all used above)

```
python tools/dis.py 0x1B53 0x40          # the field music issuer
python tools/dis.py 0xF4 0x30            # the loader chain C0/00F4
python tools/dis.py 0x1A5 0x28           # the menu-return reload (no 092B/1B53)
python tools/dis.py 0x92B 0x40           # loc*14
python tools/dis.py 0x3C11 0xC0          # VM ops E8/EA/EB/EC/ED/EE
python tools/dis.py 0x1BAB 0x40          # battle start ; 0x1BE6 0x80 = battle restore ; 0x1C90 = game over
python tools/dis.py 0xD039E 0x30 ; 0xD03B4 0x38 ; 0xD0D40 0xA0   # battle fade / gate / jingle
python tools/dis.py 0x22F00 0x60 ; 0x22FF0 0x60 ; 0x262ED 0x40   # overworld
python tools/dis.py 0x70140 0x120 ; 0x70734 0x50 ; 0x708E3 0x30  # the C7 dispatcher

build/ct.exe --read "loc,wram:7E29AE+1,wram:7E02AE+1,wram:001E00+8" --from saves/truce_market.state
build/ct.exe --teleport 439 8 8 0 --from saves/truce_market.state --frames 180 --out tp.state
build/ct.exe --monitor "1E00:8,7E29AE:1,7E01FA:1,7E0100:2" --from saves/battle_re/forest_holdleft_pre.state \
             --btnscript "40:180,100:16,0:16,...(x40)" --frames 1600 --diff --writer
build/ct.exe --read "loc,wram:7E1B9B+16,wram:7E02AE+1" --from saves/ow_w0.state
```
(`--btnscript`/`--input` buffers are 512 chars — longer scripts are silently truncated.)


---

## THE OVERWORLD SONG — mechanism READ, source REFUTED (2026-07-25)

The overworld's music does NOT come through `C0/1B53`. `C0/0066` decides it:

```
0066  LDX $0100 / CPX #01F0 / BMI ->0072 / JML $C2:0000
```

so any location >= `$1F0` jumps straight into the overworld module and never reaches the field
loader's song code at all. The overworld has its own pair of routines, both fully readable:

```
C2/2F21  JSR C2/62ED        ; -> the region index
C2/2F25  LDA $1B9B,X        ; the region's SONG id
C2/2F28  STA $1E01
C2/2F2B  STA $02AE          ; <- $02AE, NOT $29AE, is the overworld song shadow
C2/2F3A  LDA #10 / STA $1E00 ... JSL $C7:0004      (the $1E02=$FF/$1E03=$80 staging is
                                                    overwritten by the loader's own record read)
C2/3009  LDA $1B9B,X / CMP $02AE / BEQ ->3034      ; walking: same song -> nothing to do
C2/3011  ... else arm the change ($1BE8=81 $1BE9=0C $1BEA=0 $1BEB=FF $1BED=0, JSR C2/2ED9)
```

**~~`$1B9B` has NO WRITER~~ — RETRACTED, the writer is FOUND (2026-07-25).** The static reading was
right about the instructions and wrong about the conclusion: an opcode scan for every store form
(`8D/9D/99/8F/9F/9C/9E` + `$1B9B`) really does return ZERO hits, and the only *instruction* accessors
are four `LDA $1B9B,X` in bank `$C2` (`C2/2F25`, `C2/2F6A`, `C2/3009`, `C2/307A`). But "no store
names this address" is not "nothing writes it".

Caught live with a CPU write hook across an actual world load
(`CT_OWWRITEWATCH=1B9B ct.exe --owforce <state> <world>`):

```
world 0: $1B9B = 01   by C2/12C8        world 1: $1B9B = 02   by C2/12C8
world 4: $1B9B = 22   by C2/12C8        world 5: $1B9B = 03   by C2/12C8
world 7: $1B9B = 05   by C2/12C8        (and $1B9C = 1E on world 0, same site)
```

The reported PC is the one AFTER the store retires; the store itself is `C2/12C5`, inside a generic
**script POKE opcode**:

```
C2/12B8  LDY #0001 / LDA [$58],Y / TAX     ; X = a 16-bit ADDRESS, read from the script at [$58]
C2/12C0  SEP #20 / LDY #0003 / LDA [$58],Y ; A = the byte VALUE, also from the script
C2/12C5  STA $0000,X                       ; *** the write ***
C2/12CA  LDA #0004 / RTS                   ; opcode length 4
```

So `$1B9B` is filled by the OVERWORLD'S OWN INIT SCRIPT through an address-and-value poke. The
address is script DATA, never an instruction operand -- which is exactly why every static scan
missed it, and why "the table has no locatable loader" was the wrong conclusion to draw from a
scan alone.

**What remains to port the overworld song:** the per-world init script and the poke sequence it
runs (plus `C2/62ED`, the region index). Both are now ordinary derivation targets rather than a
dead end. The `--owforce` write-watch that found this is permanent (`CT_OWWRITEWATCH=<hexaddr>`);
a world load is the only event that repopulates the `$7E1B00` page, and no other mode could see it.

### ⛔ A PLAUSIBLE SHORTCUT, TESTED AND REFUTED

`$1B9B[0]` is the world's entry song (and `$02AE` mirrors it). Forcing each world with
`ct.exe --owforce` and reading it gives:

| world | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `$1B9B[0]` | 01 | 02 | 05 | 04 | **22** | **03** | **03** | **05** |
| `$F6[(0x1F0+w)*14]` | 01 | 02 | 05 | 04 | 03 | 00 | 00 | 00 |

The first FOUR worlds match exactly, which is more than enough to look like a discovery — "the
overworld song is byte +0 of the same 14-byte location record the field loader uses". **It is not.**
Worlds 4-7 disagree, so the agreement on 0-3 is a coincidence of the common eras. Implementing the
`$F6` reading would have been invention that passed a four-case check.

The observed sequence `01 02 05 04 22 03 03 05` also does not appear in the ROM as a table at any
tested stride (1, 2, 3, 4, 14), so it is not a simple world-indexed array either.

**Consequence, and it is the honest one:** the 1000 A.D. new-game pan and every other overworld stay
SILENT in the port. That is a real gap, not correct behaviour — but closing it needs the code that
builds `$1B9B`, and nothing short of that can be implemented without inventing the source.
*What would close it:* a `--monitor "1B9B:8" --writer` run captured ACROSS an overworld entry (the
existing states are all post-entry, and `--owforce` pokes + jumps rather than reporting writers), or
tracing `C2/62ED` and the world-load path that fills the `$7E1B00` page.


### The script, and the opcode, located (same pass)

Knowing the poke's shape (`[op][addrLo][addrHi][value]`, length 4) turns the search into a data
search. Every `9B 1B <observed value>` in the ROM is preceded by the SAME opcode byte, **`0x13`**,
and every site lies in bank **`$C3`**:

| world | value | site (flat / bank) |
|---|---|---|
| 0 | `01` | `0x038DB2` = `C3/8DB2` |
| 1 | `02` | `0x0399F6` = `C3/99F6` |
| 2 | `05` | `0x03A035` or `0x03C876` (ambiguous, shared value) |
| 3 | `04` | `0x03A7F8` = `C3/A7F8` |
| 4 | `22` | `0x03AD7A` = `C3/AD7A` |
| 5 | `03` | `0x03C04A` (shared with world 6) |
| 6 | `03` | `0x03C04A` |
| 7 | `05` | `0x03A035` or `0x03C876` |

So **overworld script opcode `$13` = POKE(addr16, byte)**, executed by the handler at `C2/12B8`
(store `C2/12C5`), and the overworld's per-world INIT SCRIPTS live in bank `$C3`.

**Honest limits of this:** worlds sharing a song value (2/7 at `$05`, 5/6 at `$03`) cannot be told
apart by a data search alone -- only ten `13 9B 1B xx` sites exist for eight worlds. Resolving them,
and porting the song properly, needs the per-world SCRIPT ENTRY table in bank `$C3` and the
interpreter's dispatch, not another pattern match. That is now an ordinary derivation target: the
mechanism, the opcode, the handler and the data bank are all pinned.

**Do NOT implement "scan the script for a `13 9B 1B` poke"** as a shortcut. It would be pattern
matching on ROM bytes rather than executing the ROM's own program, it is ambiguous for four of the
eight worlds on this ROM alone, and it would silently miss any world whose script sets the song
through a different opcode.


### The overworld SCRIPT VM itself, derived (the front is no longer blocked)

Watching `$0058` -- the interpreter's script pointer -- across the same world load
(`CT_OWWRITEWATCH=0058 ct.exe --owforce ...`) shows it advanced by `C2/0F73` and `C2/0F84`, which
leads straight to the whole interpreter. Twenty instructions:

```
C2/0F63  LDX $4E                  ; X = the script OBJECT's base
C2/0F67  LDA $0009,X / STA $5A    ; script pointer BANK  <- object +9
C2/0F6E  LDA $0007,X / STA $58    ; script pointer ADDR  <- object +7 (16-bit)
C2/0F73  LDA [$58] / AND #00FF    ; fetch the opcode
C2/0F78  ASL A / TAX
C2/0F7A  JSR ($0F91,X)            ; *** dispatch table at C2/0F91, opcode*2 ***
C2/0F7D  BEQ ->0F8E               ; handler returned 0 -> yield
C2/0F7F  CLC / ADC $58 / STA $58  ; advance by the LENGTH the handler returned in A
C2/0F84  LDX $4E / STA $0007,X    ; write the advanced pointer back into the object
C2/0F89  STZ $0032,X
C2/0F8C  BRA ->0F73
```

The dispatch table at `C2/0F91` is dense and regular; entry `$13*2` reads `C2/12B8`, exactly the
POKE handler found above, whose `LDA #0004 / RTS` is the length the loop then adds. Dumped:

```
op 00->1037  01->1091  02->10A8  03->10BF  04->10CD  05->1100  06->1169  07->116B
op 08->11B2  09->11F6  0A->120C  0B->121D  0C->1230  0D->1243  0E->1257  0F->126D
op 10->1285  11->1296  12->12A7  13->12B8  14->12CE  15->12E7  16->1302  17->131C
op 18->1336  19->1350  1A->136A  1B->1375  1C->1398  1D->13B8  1E->13D8  1F->13FC
```

**This is the SAME VM the port already runs.** `src/world.c:359`'s ambient-object switch cites
handlers from this very table (`op 0x30` = `C2/1CDA`, `op 0x32` = `C2/164B`, `op 0x3B` = `C2/18C7`).
The difference is the SCRIPT SOURCE: our `CtWorldAmbientFrame` reads scripts out of WRAM bank `$7F`
(`ram + 0x10000 + pc`), whereas the world INIT script -- the one that pokes `$1B9B` -- lives in ROM
bank `$C3` and is reached through the object's `+7`/`+9` pointer, which the world loader sets.

**So what is left is precise and small in kind, not in kind unknown:** run the bank-`$C3` world init
script through the interpreter the port already has, which means deriving how the loader fills the
object's `+7`/`+9` for a world. Everything else -- interpreter, dispatch table, opcode semantics,
length convention, the poke itself -- is pinned above.

**Still NOT to be shortcut:** adding an `op $13` case to `CtWorldAmbientFrame` would be inert, since
the `$7F` ambient scripts are not the script that pokes `$1B9B`. The gap is the script SOURCE, not
a missing opcode.


### Scale check on that last link (read before estimating it as small)

Scanning a post-load overworld WRAM for script objects whose bank field (`base+9`) holds `$C3` finds
**fourteen** of them, on a 0x40 stride: bases `$0C34 $0CB4 $0D34 $0D74 $0F34 $0F74 $0FF4 $1034 $1074
$10B4 $11B4 $11F4 $1234 $1274`, pointing at `C3/E6CE`, `C3/FA6D`, `C3/F836`, `C3/E16A`, `C3/F0F6`, …

So the overworld does not run "a world init script" -- it runs a POPULATION of bank-`$C3` scripts
through this VM, concurrently, the same way the field runs its object scripts. The port currently
runs only the `$7F` ambient ones (`CtWorldAmbientFrame`).

That reframes the remaining work honestly: it is not one missing pointer, it is bringing the `$C3`
script population into the port's overworld. Worth doing -- it is presumably where much more than the
song lives -- but it should be scoped as a feature, not as the tail of the audio work.
