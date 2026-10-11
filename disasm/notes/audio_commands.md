# The runtime SOUND-COMMAND protocol (CPU → Akao driver)

**Front 2 of 4, AUDIO.** How the running game asks the already-booted SPC700 driver to play a
song or a sound effect. Companion to `apu_upload.md` (which covers the boot-time IPL upload of
the driver into ARAM; that is a *different* protocol and is NOT what the game uses at runtime).

Everything below is either (a) disassembled with `python tools/dis.py <flatoffset> <len> [m] [x]`
(flat offset = `(bank-0xC0)*0x10000 + off`, so `$C7:061C` → `0x7061C`), or (b) an observed
`build/ct.exe` value. Every claim carries its citation. Gaps are listed in §12.

---

## 1. The mailbox — `$7E:1E00`

The whole runtime interface is a **4-byte RAM mailbox plus two state bytes**. There is no
"pass it in A/X" convention: the caller fills WRAM, then does one `JSL`.

| addr | role | citation |
|---|---|---|
| `$7E:1E00` | **command byte** → written to `$2140` (APUIO0). Zeroed by the driver on exit. | `C7/062D LDA $00` / `C7/0632 STA $2140`; `C7/0194 STZ $00` |
| `$7E:1E01` | **param 1** → `$2141` (APUIO1). Usually the song id / SFX id. | `C7/0628 LDA $01` / `C7/062A STA $2141` |
| `$7E:1E02` | **param 2** → `$2142` (APUIO2) | `C7/0623 LDA $02` / `C7/0625 STA $2142` |
| `$7E:1E03` | **param 3** → `$2143` (APUIO3) | `C7/061E LDA $03` / `C7/0620 STA $2143` |
| `$7E:1E05` | **driver state**: `$E0` = idle/ready, otherwise = the song id currently being uploaded. **Gate**: a command is only accepted when bit 7 is set. | `C7/0157 LDA $05 / C7/0159 BPL ->0192`; set `$E0` at `C7/0139-013B` (boot) and `C7/0601-0614` (end of song load); set to the song id at `C7/01B1-01B3` |
| `$7E:1E10/$1E11` | music **pause shadow** (cmd `$70`) | `C7/08E5-08F3`; `C0/1BC1 LDA #FF / STA $1E10` |
| `$7E:1EF0` | current **SFX bank** index | `C7/0128-012A`, `C7/0A00` |
| `$7E:1EF2` | derived voice threshold `= (0xF1 - bank*8) & 0xFF` | `C7/012A-0135` (`ASL×3; EOR #FF; INC; CLC; ADC #F1`) |

Inside bank `$C7` the direct page is `$1E00` (`C7/0150 LDX #1E00 / PHX / PLD`), so `$00`..`$05`
in that bank's listings are exactly `$7E:1E00`..`$7E:1E05`. Callers in banks `$C0/$C1/$C2/$CD/$FF`
write the same bytes as absolute `$1E00`..`$1E03` (DBR = `$00`, i.e. the low-WRAM mirror of `$7E`).

**Oracle confirmation of `$1E05`** — every playable state in the repo reads `$1E05 = $E0`:

```
./candy_shop_lake.state      loc 1B7  $1E05 = E0
./build/gato_idle400.state   loc 006  $1E05 = E0
./bedslide.state             loc 002  $1E05 = E0
./out_s8.state               loc 008  $1E05 = E0
   (build/ct.exe --read "wram:1E05+1" --from <state>)
```

**Oracle confirmation of `$1EF2 = 0xF1 - bank*8`** (3 independent states,
`--read "wram:1EF0+4"`):

```
candy_shop_lake.state   $1EF0+4 = 05 00 C9 00     bank 5  -> 0xF1-0x28 = 0xC9  OK
build/gato_idle400.state $1EF0+4 = 04 00 D1 00    bank 4  -> 0xF1-0x20 = 0xD1  OK
bedslide.state          $1EF0+4 = 0F 00 79 00     bank 15 -> 0xF1-0x78 = 0x79  OK
```

---

## 2. The single entry point — `JSL $C7:0004`

`$C7:0004` is `4C 40 01 = JMP $0140` (entry table documented in `apu_upload.md`, re-verified).
**Every** runtime sound in the game goes through it: a ROM-wide search for `22 04 00 C7` finds
exactly **59** call sites and no others (§9). `$C7:0000` (the IPL upload) has exactly one caller,
`C0/0053`, i.e. boot only.

Prologue/epilogue (`tools/dis.py 70140 0x120 1 0`):

```
C7/0140  PHB PHD PHP           ; save
C7/0143  REP #20 / REP #10 / PHA PHX PHY
C7/014A  SEP #20 / LDA #00 / PHA / PLB     ; DBR = $00
C7/0150  LDX #1E00 / PHX / PLD             ; DP  = $1E00   <-- the mailbox becomes direct page
C7/0155  SEP #20
C7/0157  LDA $05                           ; $1E05
C7/0159  BPL ->0192                        ; NOT ready (a song upload is in flight) -> DROP the command
C7/015B  LDA $00                           ; the command byte
C7/015D  BEQ ->0192                        ; cmd 0 -> nothing
C7/015F  BMI ->017A                        ; cmd >= $80  -> raw port write
C7/0161  CMP #10 / BCC ->0192              ; cmd < $10   -> ignored
C7/0165  CMP #18 / BCS ->0176
C7/0169  AND #0F / ASL A / XBA / LDA #00 / PHA / XBA / PHA / PLX
C7/0173  JMP ($0AD9,X)                     ; cmd $10-$17 -> jump table (bank K = $C7)
C7/0176  CMP #30 / BCS ->017D
C7/017A  JMP C7/061C                       ; cmd $18-$2F -> raw port write
C7/017D  CMP #40 / BCS ->0184
C7/0181  JMP C7/071D                       ; cmd $30-$3F -> canned macro, re-dispatch
C7/0184  CMP #70 / BNE ->018B
C7/0188  JMP C7/08E3                       ; cmd $70     -> music pause/resume
C7/018B  CMP #71 / BNE ->0192
C7/018F  JMP C7/0755                       ; cmd $71     -> load an SFX bank
C7/0192  SEP #20 / STZ $00                 ; <-- COMMON EXIT: clears $1E00 (command consumed)
C7/0196  REP #20 / REP #10 / PLY PLX PLA PLP PLD PLB
C7/01A0  RTL
```

Two consequences the C port must model:

* **`$1E00` is cleared on every exit**, including the dropped-command paths. A dropped command
  leaves no trace.
* **`$1E05` is a hard mutex.** While a song is loading (`$1E05` = song id, bit 7 clear) *every*
  other sound command is silently discarded. This is observable — see §10.3 (frames 227→286).

---

## 3. Command-id NAMESPACE (the answer to "song vs SFX")

**There is no high bit on the id and no separate port.** Song vs SFX is decided by the
**command byte in `$1E00`** (= the byte written to `$2140`). The id in `$1E01` is interpreted
against a *different* table depending on which range the command falls in.

| `$1E00` | dispatch | meaning |
|---|---|---|
| `$00` | exit | no command |
| `$01-$0F` | exit | ignored (`C7/0161 CMP #10 / BCC`) |
| `$10,$11,$14,$15` | `C7/01A1` | **LOAD + PLAY SONG**, `$1E01` = song id (`0..$52`) |
| `$12,$13,$16,$17` | `C7/0192` | table entries point at the exit = **no-ops** |
| `$18-$2F` | `C7/061C` | **raw 4-byte port command** — this is the SFX family |
| `$30-$3F` | `C7/071D` | macro: expands to (`$10`, id `$00-$0F`, `$FF`, `$FF`) then re-dispatches |
| `$40-$6F` | exit | ignored |
| `$70` | `C7/08E3` | music **pause/resume**, `$1E01` = 0/1 |
| `$71` | `C7/0755` | load an **SFX bank**, `$1E01` = bank index |
| `$72-$7F` | exit | ignored |
| `$80-$FF` | `C7/061C` | **raw 4-byte port command** (driver-level: `$80-$83`, `$F3`, `$F5`, `$FC`…) |

The `$10-$17` jump table lives at `$C7:0AD9` (`JMP ($0AD9,X)`, X = `(cmd&0x0F)*2`, pointer
fetched from program bank `$C7`). Raw bytes `d[0x70AD9..]`:

```
$C7:0AD9:  A1 01  A1 01  92 01  92 01  A1 01  A1 01  92 01  92 01
    cmd:    $10    $11    $12    $13    $14    $15    $16    $17
  target:  01A1   01A1   0192   0192   01A1   01A1   0192   0192
```

The `$30-$3F` macro table is 16 × 4 bytes at `$C7:0A98` (`C7/071F AND #000F / ASL / ASL / TAX;
LDA $C70A9A,X -> $1E02(16-bit); LDA $C70A98,X -> $1E00(16-bit); JMP $0155`):

```
cmd $30 -> $1E00=10 $1E01=00 $1E02=FF $1E03=FF
cmd $31 -> $1E00=10 $1E01=01 $1E02=FF $1E03=FF
   ... (id = cmd - $30) ...
cmd $3F -> $1E00=10 $1E01=0F $1E02=FF $1E03=FF
```

So `$30-$3F` are literally "play song 0..15" shorthands.

**Encoding rule (cite this one):** a song id is validated against the count at `$C7:0AE9`
(`= 0x53`, i.e. 83 songs) at `C7/01A5 CMP $C70AE9 / C7/01A9 BCS ->0192`; an SFX id is *never*
range-checked on the CPU side — it goes straight out of `$2141`. Songs are a CPU-side
data-upload operation; SFX are a 4-byte message.

Within the SFX family, `$18-$2F` is a **24-entry command family**, not one command. The battle
engine *computes* which one to use: `C1/61A4 SEC / LDA $9895 / SBC #$78 / CLC / ADC #$18 /
STA $1E00` (and `C1/61D3` with `SBC #$7A`) — i.e. `cmd = $9895 - $60`. `C7/061C` treats them all
identically (raw port write), so the differentiation is made by the SPC-side driver.

---

## 4. `$C7:061C` — **THE routine the C port must call**

`tools/dis.py 7061C 0x40 1 0`, raw bytes
`[57 ROM bytes at 0x7061C: python tools/dis.py 0x7061C 57]`:

```
C7/061C  SEP #20
C7/061E  LDA $03          ; $1E03
C7/0620  STA $2143        ; APUIO3   <-- written FIRST
C7/0623  LDA $02
C7/0625  STA $2142        ; APUIO2
C7/0628  LDA $01
C7/062A  STA $2141        ; APUIO1
C7/062D  LDA $00
C7/062F  LDX #0400        ; 1024-iteration timeout
C7/0632  STA $2140        ; APUIO0   <-- the COMMAND, written LAST = the trigger
C7/0635  CMP $2140        ; busy-wait for the SPC to ECHO the command byte back on port 0
C7/0638  BEQ ->0643       ; ACK
C7/063A  DEX
C7/063B  BNE ->0635
C7/063D  LDA $00          ; timeout:
C7/063F  BEQ ->0652       ;   $1E00 cleared meanwhile -> give up
C7/0641  BRA ->061C       ;   otherwise RE-SEND all four bytes, from the top (unbounded retry)
C7/0643  CMP #FC          ; ack path: was this the "set SFX bank" command?
C7/0645  BNE ->0652
C7/0647  LDA $01 / AND #0F / CMP $F0 / BEQ ->0652
C7/064F  JSR C7/09FD      ;   -> $1EF0 = bank, $1EF2 = 0xF1 - bank*8, JSR $0A12 (voice cull)
C7/0652  JMP C7/0192      ; common exit (clears $1E00)
```

**Exact write order: `$2143`, `$2142`, `$2141`, `$2140`, then spin on `CMP $2140`.**
Nothing is written to `$2144+` (they don't exist) and no read of `$2141-$2143` happens here.

Note `$1E03` is **persistent state**: several callers (including field opcode `$E8`) never write
it, so `$2143` carries whatever the previous command left there. This is not a bug to normalize
away — it is observable (§10.4) and the port must keep `$1E03` as sticky RAM.

The same echo handshake, factored, is `C7/09DA` (`STA $2140 / CMP $2140 / BNE / INC A / AND #7F`)
— used by the *data-streaming* paths with a rolling 7-bit counter instead of a command byte.

---

## 5. The song path — `$C7:01A1` (cmd `$10/$11/$14/$15`)

```
C7/01A1  SEP #20
C7/01A3  LDA $01                  ; song id
C7/01A5  CMP $C70AE9              ; d[0x70AE9] = $53 -> 83 songs
C7/01A9  BCS ->0192               ; out of range -> drop
C7/01AB  JSR C7/0734              ; cmd >= $14 -> $1E00 |= $04 ; else scan list $C7:0AD8
C7/01AE  JSR C7/0A39              ; song record lookup + first port block
C7/01B1  LDA $01
C7/01B3  STA $05                  ; $1E05 = song id  ==> "BUSY", blocks all further commands
   ... multi-frame upload of sequence + instrument set to ARAM via $2141/$2142/$2143 with the
       C7/09DA counter handshake (C7/01B5-C7/0613) ...
C7/0601  LDA #E0 / LDX #0400
C7/0606  STA $2141 / STA $2140 / CMP $2140 / (spin)
C7/0614  STA $05                  ; $1E05 = $E0  ==> "READY" again
C7/0616  JSR C7/0A12 / C7/0619 JMP C7/0192
```

`C7/0A39` is the record lookup that the oracle lets us verify byte-for-byte:

```
C7/0A3C  LDA $01 (16-bit) / AND #007F / ASL A / TAX
C7/0A45  LDA $C7241E,X / STA $02        ; $1E02 = d[0x7241E + id*2]
C7/0A4B  LDA $C7241D,X / AND #0F / STA $03   ; $1E03 = d[0x7241D + id*2] & $0F
C7/0A53  CMP $F0 / BEQ +5 / JSR C7/09FD  ; bank switch if the song needs another SFX bank
C7/0A5A  LDA $03 -> $2143 / LDA $02 -> $2142 ...
```

**Song table = `$C7:241D + id*2`** (83 entries). Four independent oracle cross-checks:

| song | ROM `$1E02/$1E03` | oracle state | observed `$1E02/$1E03` |
|---|---|---|---|
| `$00` | `FF / 05` | `gato_press_action` f146 (op `$EA`, id 0) | `FF / 05` ✔ |
| `$06` | `C8 / 05` | `candy_shop_lake` (loc 439) | `C8 / 05` ✔ |
| `$1E` | `E8 / 0D` | `build/leg1.state` (loc 2) | `E8 / 0D` ✔ |
| `$42` | `CC / 0A` | `gato_press_action` f0 | `CC / 0A` ✔ |
| `$45` | `B0 / 04` | `gato_press_action` f227 (battle music) | `B0 / 04` ✔ |

`C7/0734`'s song-exception list is `$C7:0AD8`, whose first byte is `$FF` → `BMI` terminates the
scan immediately, so in this ROM the routine only ever does `if (cmd >= $14) $1E00 |= 4`
(`$14|4 == $14`, `$10|4 == $14`). No CPU-side consumer of bit 2 was found in `$C7:01A1-$C7:061C`
(scan for `A5/24/C5 00` in that range returned nothing) — see §12.

---

## 6. FIELD-VM sound opcodes

The field-VM dispatch table is at **`$C0:5D6E`**, 256 × 2-byte handler addresses, reached by
`0A AA E2 20 FC 6E 5D` = `ASL A; TAX; SEP #$20; JSR ($5D6E,X)` at `C0/5910`, `C0/5977`,
`C0/59A0`, `C0/5AB1`. Handler `RTS` returns **CLC = yield this visit**, **SEC = keep running**
(cross-check: op `$E3` ends `SEC` and is the non-yielding `$011F` write already ported).

A ROM-wide scan for `22 04 00 C7` puts exactly six handlers in the sound family, and a ROM-wide
scan for `$2140-$2143` accesses finds only two more reads in bank `$C0` (`C0/3CBD`, `C0/3CC6`) —
both are VM handlers. **That is the complete set.**

| op | handler | len | what it does |
|---|---|---|---|
| `$E8` | `C0/3C11` | 2 | `$1E01 = operand; $1E02 = $80; $1E00 = $18; JSL $C7:0004` → **play SFX**. `$1E03` untouched (stale). Ends `INX; CLC` = yield. |
| `$E9` | `C0/5F6E` | 1 | unassigned (shared no-op handler) |
| `$EA` | `C0/3C2F` | 2 | `$7E29AE = operand` (**song shadow**); `$1E01 = operand; $1E00 = $10; JSL` → **play song**. `CLC`. |
| `$EB` | `C0/3C4C` | 3 | three raw commands back-to-back: (`$81`, op1, op2, `$FF`), then (`$82`, `$00`, `$FF`), then (`$83`, `$00`, `$FF`). `CLC`. |
| `$EC` | `C0/3C98` | 4 | **raw escape**: `$1E00 = op1; $1E01 = op2; $1E02 = op3; JSL`. Any command byte, incl. `$10`. `$1E03` stale. `CLC`. |
| `$ED` | `C0/3CBC` | 1 | `TYX; LDA $2141; BNE +1; INX` → **wait until `$2141 == 0`** (re-executes itself while non-zero). `CLC`. |
| `$EE` | `C0/3CC5` | 1 | `TYX; LDA $2143; AND #0F; BNE +1; INX` → **wait until `($2143 & $0F) == 0`**. `CLC`. |
| `$EF` | `C0/5F6E` | 1 | unassigned |

Raw handler bytes (for the record):
`C0/3C11: [30 ROM bytes at 0x3C11: python tools/dis.py 0x3C11 30]`
`C0/3CBC: BB AD 41 21 D0 01 E8 18 60` · `C0/3CC5: BB AD 43 21 29 0F D0 01 E8 18 60`

Operand addressing: the handlers read `$7F2001,X` with X = script index; `INY/TYX` walks the
operands and the post-`JSL` `LDX $C7 / INX` sets the next IP — which is exactly the OPLEN the
C port already carries (`src/ct_vm.c:16` OPLEN row `0xE0-0xEF` = `5,5,5,2,8,8,5,3,2,1,2,3,4,1,1,1`
→ `$E8`=2, `$E9`=1, `$EA`=2, `$EB`=3, `$EC`=4, `$ED`=1, `$EE`=1, `$EF`=1). **The C lengths already
match the ROM byte-for-byte.**

No other field opcode reaches sound indirectly: the bank-`$C0` sound helpers `C0/1B53`,
`C0/1B90`, `C0/1BA7` have callers `C0/00F7` / `C0/0D92,0E8F,1040,1212,149D` / `C0/16EB,D7/85CE`
respectively — none inside the `$C0:2E00-$C0:5600` handler region.

---

## 7. Location → song, and the shadow `$7E:29AE`

`C0/1B53` (raw `AF ED 01 7F F0 06 AF AE 29 7E 80 24 A9 00 8D 01 1E ...`), the **only** caller
being `C0/00F7`:

```
C0/1B53  LDA $7F01ED
C0/1B57  BEQ ->1B5F            ; 0 = normal load
C0/1B59  LDA $7E29AE           ; else re-use the shadow (returning from battle/menu)
C0/1B5D  BRA ->1B83
C0/1B5F  LDA #00 / STA $1E01
C0/1B64  LDA #FF / STA $1E02
C0/1B69  LDA #FF / STA $1E03
C0/1B6E  LDA #81 / STA $1E00
C0/1B73  JSL $C7:0004          ; cmd $81, ($00,$FF,$FF)  = "stop / prepare"
C0/1B77  LDX $FE               ; DP=$0100 -> $01FE
C0/1B79  LDA $F60000,X         ; per-location song byte
C0/1B7D  BMI ->1B8F            ; bit7 set -> DO NOT change the song
C0/1B7F  STA $7E29AE           ; song shadow
C0/1B83  STA $1E01
C0/1B86  LDA #10 / STA $1E00
C0/1B8B  JSL $C7:0004          ; cmd $10 = load + play
C0/1B8F  RTS
```

`$01FE = loc * 14` — the location-descriptor offset; the **song is byte +0 of the 14-byte
location record in ROM bank `$F6`**. Verified against the oracle (`--read "wram:01FE+2,loc,wram:29AE+1"`):

| state | loc | oracle `$01FE` | `loc*14` | ROM `F6/[loc*14]` | oracle `$29AE` |
|---|---|---|---|---|---|
| `bedslide.state` | `002` | `001C` | `0x1C` | `FF` (= no change) | `0F` (set by a script op `$EA`) |
| `build/leg1.state` | `002` | — | `0x1C` | `FF` | `1E` (different script) |
| `base5_ag1.state` | `005` | `0046` | `0x46` | `06` | `06` ✔ |
| `out_s8.state` | `008` | `0070` | `0x70` | `06` | `06` ✔ |
| `candy_shop_lake.state` | `1B7` | `1802` | `0x1802` | `06` | `06` ✔ |

The `FF` at loc 2 is exactly why Crono's bedroom shows two different `$29AE` values in two
different repo states — the location table declines to set a song and the scene script picks it
with op `$EA`.

**`$7E:29AE` is definitively the current-song shadow.** All accessors, ROM-wide:
`C0/1B59` (re-use), `C0/1B7F` (loc song), `C0/1C2E` (post-battle restore via cmd `$11`),
`C0/3C35` (field op `$EA`), `C2/2C50/2F30/2F72/2FB4` (menu reads it, `C2/2F49` replays it with
cmd `$10`), `C2/7BD3`, `C2/CB9E`, `F0/C7E1`.

**`$7E:29AD` is NOT part of the sound protocol.** Its only accessors are `C1/800A` (→ `$26` /
`$B3E6`, battle boot), `C1/8445`, `C2/805E`, `C2/E6A8`, `E8/978E` — none of them reads or writes
`$1E00-$1E03` in the same block, and none appears in the 59-site `JSL $C7:0004` list. It is a
battle/menu byte that merely happens to sit next to the song shadow.

---

## 8. The other CPU-side sound helpers worth naming

* `C0/1B90` / `C0/1BA7` — cmd `$19`, `$1E01 = $01FC` (resp. `$01FB`), `$1E02 = $0A00,Y` with
  `Y = $0197` (the lead PC's slot): a **positional/panned SFX**.
* `C0/1BAB` (RTL) — **battle enter**: if `$7E2A1F` bit 6 clear → `$1E10 = $FF; $1E01 = $01FA;
  cmd $14` (= play the battle song); else → cmd `$70` with `$1E01 = 1` (just pause the field song).
* `C0/1BE6` (RTL) — **battle exit**: cmd `$81(00,00,FF)`, then cmd `$11` with the shadow
  `$7E29AE`, then `$81(40,FF,FF)`, `$82(00,FF)`, `$83(00,FF)`; the `$2A1F` bit 6 variant instead
  issues cmd `$70` with `$1E01 = 0` (resume). The game-over branch uses cmd `$14` with `$1E01 = $26`.
* `CD/3B71` — **the generic battle SFX call**: `STA $1E01` (A = id), `LDA #18 / STA $1E00`,
  `TXA / STA $1E02` (X = param 2), `JSL`.
* `C1/1B43` / `C1/1B55` — battle cursor SFX: cmd `$19`, `$1E02 = $80`, `$1E01 = $42` resp. `$00`.
* `C1/40D3` — `$1E01 = $FF`, cmd `$18` = **stop all SFX** (gated on `$9876`).
* `C1/61A1` / `C1/61C1` / `C1/6200` — the battle animation-script sound ops: id from the
  script stream `[$E6],Y`, command **computed** as `$9895 - $60` (`$18`-family slot select).
* `C2/EAF9` (cmd `$19`, A = id, clears the `$0D76` latch first) and `C2/EB03` (cmd `$18`,
  A = id, respects the latch) — **the menu SFX API**. `$0D76` is a one-SFX-per-window latch
  (`LDA $0D76 / BNE skip / LDA #03 / STA $0D76`). Named wrappers:
  `C2/EABA` = play A; `C2/EAC2` = **SFX `$00` (cursor move)**; `C2/EACC` = **SFX `$01`**;
  `C2/EAD6` = SFX `$06`/`$07` selected by `$0DBC` bits 1/0.
* `C2/80A9-80EA` — **menu entry sound reset**: cmd (from `C2/70A9`) with `$1E01=0`, then
  `$18(FF,80)`, `$19`, `$82(01,FF)`, `$83(01,FF)`.
* `C2/2F20-2F49` — **menu exit music restore**: `$1E01` = `$1B9B,X` or the shadow `$7E29AE`,
  then cmd `$10` with `$1E02=$FF`, `$1E03=$80`.

---

## 9. Complete call-site catalogue (all 59 `JSL $C7:0004`)

Heuristic back-scan (literal `LDA #imm / STA $1E0x` within 72 bytes; `<reg>` = value came from a
register/memory, `-` = not written by this site so the byte is stale):

```
C0/1B73 cmd 81 (00,FF,FF)   C0/1B8B cmd 10 (song,--,--)  C0/1BA2 cmd 19 (reg,reg,-)
C0/1BCE cmd 14 (reg,reg,-)  C0/1BDF cmd 70 (01,-,-)      C0/1C2A cmd 81 (00,00,FF)
C0/1C3A cmd 11 (29AE,00,FF) C0/1C52 cmd 81 (40,FF,FF)    C0/1C65 cmd 82 (00,FF,-)
C0/1C78 cmd 83 (00,FF,-)    C0/1C89 cmd 70 (00,FF,-)     C0/1CA9 cmd 81 (00,00,FF)
C0/1CB7 cmd 14 (26,00,FF)   C0/1CCF cmd 81 (80,FF,FF)    C0/1CE2 cmd 82 (00,FF,-)
C0/1CF5 cmd 83 (00,FF,-)
C0/3C26 cmd 18 (op,80,-)    <- FIELD VM op $E8
C0/3C43 cmd 10 (op,--,--)   <- FIELD VM op $EA
C0/3C69 cmd 81 (op1,op2,FF) C0/3C7C cmd 82  C0/3C8F cmd 83   <- FIELD VM op $EB
C0/3CB4 cmd <script>        <- FIELD VM op $EC
C1/1B50 cmd 19 (42,80,-)    C1/1B62 cmd 19 (00,80,-)     C1/40DD cmd 18 (FF,-,-)
C1/61B5 cmd <$9895-$60>     C1/61DF cmd <$9895-$60>      C1/620F cmd 18
C2/0462 …                   C2/23F9 …                    C2/24F0 cmd 82 (00,FF,-)
C2/2501 cmd 83 (00,FF,-)    C2/264E cmd 82 (00,FF,-)     C2/2F49 cmd 10 (song,FF,80)
C2/6BE9 cmd 18 (01,80,-)    C2/80B2 cmd 70 (00,-,-)      C2/80C5 cmd 18 (FF,80,-)
C2/80CE cmd 19 (FF,80,-)    C2/80E1 cmd 82 (01,FF,-)     C2/80EA cmd 83 (01,FF,-)
C2/C033 cmd F3 (0D86,-,-)   C2/CBF3 cmd 14 (3E,80,reg)   C2/CC09 cmd 81
C2/CF46 cmd 19 (8A,80,-)    C2/E6A1 cmd F3 ($2990&08)    C2/EB1A cmd 18|19 (A,80,-)
C3/09D1 cmd 10              C3/0BE9 cmd <reg>            C3/1647 …
CD/03AB cmd 81 (10,00,-)    CD/0D70 cmd 80 (00,FF,-)     CD/0D81 cmd 10 (3A,FF,-)
CD/3B7D cmd 18 (A,X,-)      CD/3E54 cmd F5 (reg,-,-)     CD/3E70 cmd F5 (00,-,-)
FF/FC82 cmd 70              FF/FC97 cmd 18 (reg,80,reg)  FF/FF24 cmd 70 (reg,-,-)
FF/FF39 cmd 18 (reg,80,reg)
```

---

## 10. Empirical oracle confirmation

### 10.1 A full command dispatch, instruction by instruction

`build/ct.exe --trace 900000 --from build/techmenu.state` (format `KK PPPP DP M X E`).
The window contains **18** dispatches; here is one, complete, with the caller:

```
C1 1B58 0000 1 0 0      <- C1/1B55 STZ $1E01
C1 1B5A 0000 1 0 0         LDA #19
C1 1B5D 0000 1 0 0         STA $1E00
C1 1B5F 0000 1 0 0         LDA #80
C1 1B62 0000 1 0 0         STA $1E02 / JSL
C7 0004 0000 1 0 0
C7 0140 0000 1 0 0
C7 0141 … 0143 … 0145 … 0147 … 0148 … 0149 … 014A … 014C … 014E … 014F … 0150 … 0153 … 0154
C7 0155 1E00 1 0 0      <- note DP is now $1E00
C7 0157 1E00 1 0 0         LDA $05
C7 0159 1E00 1 0 0         BPL  (not taken -> driver ready)
C7 015B 1E00 1 0 0         LDA $00
C7 015D 1E00 1 0 0         BEQ  (not taken)
C7 015F 1E00 1 0 0         BMI  (not taken -> cmd < $80)
C7 0161 1E00 1 0 0         CMP #10
C7 0163 1E00 1 0 0         BCC  (not taken)
C7 0165 1E00 1 0 0         CMP #18
C7 0167 1E00 1 0 0         BCS  (TAKEN -> cmd >= $18)
C7 0176 1E00 1 0 0         CMP #30
C7 0178 1E00 1 0 0         BCS  (not taken -> $18..$2F)
C7 017A 1E00 1 0 0         JMP $061C
C7 061C 1E00 1 0 0      <- THE port write
C7 061E 1E00 1 0 0         LDA $03
C7 0620 1E00 1 0 0         STA $2143
C7 0623 1E00 1 0 0         LDA $02
C7 0625 1E00 1 0 0         STA $2142
C7 0628 1E00 1 0 0         LDA $01
C7 062A 1E00 1 0 0         STA $2141
C7 062D 1E00 1 0 0         LDA $00
C7 062F 1E00 1 0 0         LDX #0400
C7 0632 1E00 1 0 0         STA $2140
   ... [938 spin lines: C7 0635/0638/063A/063B echo-wait] ...
C7 0643 1E00 1 0 0         CMP #FC
C7 0645 1E00 1 0 0         BNE (taken)
C7 0652 1E00 1 0 0         JMP $0192
C7 0192 1E00 1 0 0
C7 0194 1E00 1 0 0         STZ $00        <- command consumed
C7 0196 … 0198 … 019A … 019B … 019C … 019D … 019E … 019F
C7 01A0 0000 1 0 0         RTL
C1 1B66 0000 1 0 0      <- back in the caller
```

Aggregate over the whole 900k-instruction window (all 18 dispatches):

```
C7 0004:18  0140..017A:18 each   061C..0632:18 each
C7 0635: 1263   C7 0638: 1263   C7 063A: 1245   C7 063B: 1245
C7 0643: 18    C7 0645: 18    C7 0652: 18    C7 0192/0194/0196..01A0: 18
```
1263 `CMP $2140` − 18 successes = 1245 `DEX` — exactly consistent, ~70 spin iterations per
command on average. `C7/063D`/`C7/0641` (the timeout re-send) never executed → the 1024-iteration
budget was never exhausted. No `$10-$17`, `$30-$3F`, `$70` or `$71` command occurred in this window.

### 10.2 Field VM op `$EA` fires a song command (writer PCs)

`build/ct.exe --monitor "1E00:1,1E01:1,1E02:1,1E03:1,1E05:1,1E10:1,1E11:1,29AE:1,01FA:1,0100:2"
--from gato_press_action.state --input "0:3,100:3,0:600" --writer --diff`

```
  f0    1E00=00  1E01=42  1E02=CC  1E03=0A  1E05=E0  1E10=00 1E11=00  29AE=42  01FA=45  0100=0600
  f146  1E00=00(C7:0196) 1E01=00*(C0:3C3C) 1E02=FF*(C7:0A4B) 1E03=05*(C7:0A53)
        1E05=E0(C7:0616) 1E10=00 1E11=00  29AE=00*(C0:3C39)  01FA=45  0100=0600
  f227  1E00=14*(C7:0753) 1E01=45*(C0:1BC9) 1E02=B0*(C7:0A4B) 1E03=04*(C7:0A53)
        1E05=45*(C7:01B5) 1E10=FF*(C0:1BC4) 1E11=00  29AE=00  01FA=45  0100=0600
  f275  1E00=14 1E01=45 1E02=B0 1E03=04 1E05=45 1E10=01*(C7:04D0) 1E11=01*(C7:04D2)
  f286  1E00=00*(C7:0196) 1E01=45 1E02=B0 1E03=04 1E05=E0*(C7:0616) 1E10=01 1E11=01
  f301  1E00=00(C7:0196) 1E01=08*(CD:3B74) 1E02=80*(CD:3B7D) 1E03=04
  f383  1E00=00(C7:0196) 1E01=42*(C1:1B46) 1E02=80(C1:1B50)  1E03=04
  f529  1E00=00(C7:0196) 1E01=46*(C1:61A4) 1E02=80(C1:61B5)  1E03=04
```

Reading it against the disassembly (the reported writer PC is the PC *after* the store, so
`C0:3C39` = the `STA $7E29AE` at `C0/3C35`, `C0:3C3C` = the `STA $1E01` at `C0/3C39`):

* **f146 = field VM op `$EA`, operand `$00`** — the pre-battle "stop the field music". `$1E02/$1E03`
  filled by `C7/0A45/0A4B` from the song-0 record (`FF/05`) exactly as the ROM table says.
* **f227 = `C0/1BAB`, battle music start.** `$1E10 = FF` (writer `C0:1BC4` = the `STA $1E10` at
  `C0/1BC1`); `$1E01 = $45` from `$01FA` (writer `C0:1BC9` = the `STA $1E01` at `C0/1BC6`);
  `$1E00 = $14` still latched — and its **writer is `C7:0753`, the `STA $00` inside `C7/0734`
  (`LDA #04 / ORA $00 / STA $00`)** — this is the ROM's own `cmd |= 4` executing, observed.
* **`$1E05 = $45` from f227 through f285, back to `$E0` at f286** (writers `C7:01B5` = `STA $05`
  at `C7/01B3`, and `C7:0616` = `STA $05` at `C7/0614`). That is the busy mutex of §2, live, for
  59 frames. `$1E00` only returns to `00` at f286 when `C7/0194 STZ $00` finally runs.
* f301/f383/f529 are SFX through `CD/3B71`, `C1/1B43`, `C1/61A1` — the exact routines of §8.

### 10.3 Location-song load observed at boot

`build/ct.exe --findoam --watch 7E1E00 16 --frames 1` (logs every CPU write into the mailbox
over 240 frames) reports, among 65 writer PCs:

```
  writer PC C0/1B64 : 3    C0/1B69 : 3    C0/1B6E : 3    C0/1B73 : 3
  writer PC C0/1B86 : 3    C0/1B8B : 3
  writer PC C7/0A4B : 6    C7/0A53 : 6    C7/01B5 : 3    C7/0616 : 3    C7/0196 : 12
  writer PC C7/013D : 1    (the boot init that sets $1E05 = $E0)
```
i.e. `C0/1B53` ran 3 times, each time doing cmd `$81` then cmd `$10`, each song load taking the
`C7/0A45` table path and toggling `$1E05` — the §7 routine, confirmed. (`C0/1B7F`, the `$29AE`
write, is correctly absent because it is outside the watched region.)

### 10.4 Stale `$1E03` is real

`bedslide.state` reads `$1E01=40 $1E02=80 $1E03=0F $29AE=0F`. Song `$0F`'s ROM record is
`$1E02=EC, $1E03=0F`. So the last song load left `$1E03=0F`, and a later cmd-`$18`-family SFX
(id `$40`, `$1E02=$80` — the op-`$E8` signature) overwrote only `$1E01/$1E02`. The next
`C7/061C` therefore wrote `$2143 = $0F`. Contrast `build/leg1.state`
(`$1E01=1E $1E02=E8 $1E03=0D`, song `$1E` record `E8/0D`) — a clean post-load state with no SFX
since. **`$1E03` must be modelled as persistent RAM, not re-initialised per command.**

### 10.5 Menu SFX

`--monitor ... --from out_s8.state --input "0:4,200:4,0:90,20:3,0:12,100:3,0:60" --writer --diff`
(X opens the menu, then Down):

```
  f20   1E01=01*(C2:80DC)  1E02=FF*(C2:80E1)      <- C2/80D7-80E1 menu-entry reset, cmd $82(01,FF)
  f115  1E01=00*(C2:EB0B)  1E02=80*(C2:EB1A)      <- C2/EB08 STA $1E01 / C2/EB17 STA $1E02
```
The cursor-move SFX id is **`$00`**, issued by `C2/EAC2` (`LDA #00 / JSR $EB03`) → cmd `$18`,
`$1E02 = $80`.

---

## 11. Corrections to existing notes (all three anchors were wrong or unpinned)

1. **`disasm/notes/menu_phase2.md`: "cursor-move SFX #74 via C2/8255" is WRONG.**
   `C2/8255` is `PHP; SEP #30; STA $00; LDA #00; {LSR $00; BCC +; TAX; STZ $B0,X}; CLC; ADC #08;
   CMP #40; BCC loop; PLP; RTS` — a bit-mask-driven **clear of `$B0,X` slots**, no APU access
   anywhere. (Later lines of that same dossier already call it "palette fade"; that is closer,
   but in any case it is not sound.) The real cursor SFX is `C2/EAC2 → C2/EB03`, id `$00`,
   cmd `$18` — observed at f115 above.

2. **`disasm/notes/menu_phase2.md`: "A-confirm SFX #BD51 via C2/ED31" is WRONG.**
   `C2/ED31` is `PHB; PHP; SEP #20; REP #10; LDA #FF; PHA; PLB; {LDA $0000,X; BMI done; INX; TAY;
   LDA $BD18,Y; PHY; LDY #005B; MVN $7E,$FF; PLY; PHX; TYX; JSR ($ED59,X); PLX; loop}` — a
   **byte-code interpreter over a list in bank `$FF`** doing `MVN` block moves and table-dispatched
   subroutines (menu graphics staging). `$BD51` is a list pointer, not a sound id. No APU access.

3. **`disasm/notes/battle_menu_synthesis.md`: `C0/2C41` "music cue" is REFUTED.**
   `C0/2C41` sets `D=$0100`, `DB=$7F`, reads `$7F3415 BIT #02`, then integrates
   `$3417→$341D`, `$3418→$341E`, and feeds `$346B` — a **BG scroll/parallax accumulator**.
   It contains no `$1E0x` write and no `$2140-$2143` access (ROM-wide scan: bank `$C0` touches
   the APU ports only at `C0/3CBD` and `C0/3CC6`).

4. **`disasm/notes/battle_menu_synthesis.md` / memory: "`JSR C0/2824` = music fade-in" is WRONG.**
   `C0/2824` is `JSR $286C; BNE done; {INC $19; save/clear $1F; JSR C0/881E; restore $1F;
   JSR C0/00DE; JSR C0/EC60; LDA $19; CMP #0F; BMI loop}; STZ $1E; STZ $0407` — a **screen
   brightness ramp** on `$0119`, one step per frame to `$0F`. No sound.

5. **`$7E:29AD` resolved as far as sound goes:** it is *not* the song shadow and not part of the
   protocol (§7). `$7E:29AE` is the song shadow, definitively.

6. **`src/ct_vm.c:1186-1190` is right about op `$E8`** (`$1E00 = $18` JSL, then `INX; CLC`) —
   confirmed byte-for-byte at `C0/3C11`. It is incomplete only in that `$1E02 = $80` is also
   part of the op, and in that ops `$EA/$EB/$EC/$ED/$EE` are the same family (see §12/plan).

---

## 12. NOT DERIVED — remaining gaps

* **The SPC-side meaning of each id.** The driver blob at `$C7:24C3` (7151 bytes, per
  `apu_upload.md`) is SPC700 code. What SFX `$00`/`$40`/`$46` *sound* like, and what `$1E02=$80`
  means to the driver, is inside that blob. Closing it requires disassembling the uploaded ARAM
  image as SPC700 (or an ARAM dump from the oracle after boot).
* **Bit 2 of the command byte** (`C7/0734`'s `$1E00 |= 4` for cmd ≥ `$14`). No CPU-side consumer
  was found in `$C7:01A1-$C7:061C` (scan of `A5/24/C5 00` in that range = empty), and the
  exception list `$C7:0AD8` begins with `$FF` so it is inert in this ROM. It is most likely
  consumed SPC-side after being written to `$2140`. Closing it: the same SPC disassembly.
* **`$7E:9895`** (the battle "SFX slot" selector, `cmd = $9895 - $60` at `C1/61A4`/`C1/61D3`).
  Its writers were not traced. Closing it: `--monitor 9895:1 --writer` across a full battle.
* **What actually clears `$2141`/`$2143` for ops `$ED`/`$EE`.** Those are SPC writes to its
  output ports; the CPU only polls. Closing it: an APU-port monitor (no existing ct.exe mode
  reads `$2140-$2143` — see below).
* **No existing `ct.exe` mode can observe the hardware ports directly.** `--monitor --writer`
  filters writes through `ctWramOff()` (src/main.c ~4585), which rejects `$2140`; `--findoam`
  does the same. The port sequence above is proven by (a) the straight-line disassembly of
  `C7/061C`, (b) the instruction trace through every one of its four `STA`s, and (c) the exact
  source bytes `$1E00-$1E03` read out of WRAM at those moments — but not by a direct register log.
  A one-line harness addition (`--monitor` accepting `reg:2140+4`, or logging `adr&0xFFFF in
  [0x2140,0x2143]` in `cbMonitor`) would close it. That is a src/ change, out of scope here.
* **`C2/0462`, `C2/23F9`, `C3/1647`** call sites: the command byte comes from a register in each,
  not a literal; not individually traced.

---

## 13. What the C port must do (mapping to this repo)

**Today the C port has ZERO audio**: `grep -n "sound|sfx|audio|apu|2140|1E00|music" src/menu.c
src/battle.c` returns nothing audio-related (the "song" hits in `battle.c` are GATO's *musical
attack*, an action type, not sound). `src/ct_vm.c` treats op `$E8` as headless.

| observed command | ROM site | corresponding C site |
|---|---|---|
| cmd `$18`, `$1E01`=script operand, `$1E02`=`$80` | `C0/3C11` (op `$E8`) | `src/ct_vm.c:1190` — currently the `op == 0xAF \|\| 0xB1 \|\| 0xE8` yield line |
| cmd `$10` + `$29AE` shadow | `C0/3C2F` (op `$EA`) | **absent** in `src/ct_vm.c` — falls into the generic `applyEffect` tail (`ct_vm.c:1567`) |
| cmd `$81/$82/$83` triple | `C0/3C4C` (op `$EB`) | **absent**, same generic tail |
| raw (cmd,p1,p2) from script | `C0/3C98` (op `$EC`) | **absent**, same generic tail |
| wait `$2141==0` / `($2143&0F)==0` | `C0/3CBC` / `C0/3CC5` (ops `$ED`/`$EE`) | **absent**, same generic tail |
| location song from `$F6:[loc*14]` | `C0/1B53` ← `C0/00F7` | `src/scene.c` / `src/field.c` scene-load path — no equivalent |
| menu cursor SFX id `$00`, menu entry reset | `C2/EAC2`→`C2/EB03`, `C2/80A9` | `src/menu.c` — no equivalent |
| battle cursor `$19(00,80)` / confirm `$19(46,80)` / generic `$18(id,X)` | `C1/1B55`, `C1/61A1`, `CD/3B71` | `src/battle.c` — no equivalent |

**Structural note for the implementer:** the ROM's ops `$EA/$EB/$EC/$ED/$EE` all end **CLC**,
exactly like `$E8` — they yield the visit. The C VM currently yields only for `$E8`; the other
five fall through to `applyEffect(...); s->addr = nxt; return 0;` (continue). That is a
*scheduler* divergence independent of whether audio is emitted, and it is the one thing on this
front that can move `--vmdiff`. The `OPLEN` table already matches the ROM exactly, so no length
change is needed.
