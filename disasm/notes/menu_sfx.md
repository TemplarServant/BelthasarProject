# The in-game MENU's sound — full derivation

*(written to `C:\Users\Icaro Ferris\Desktop\Crono Trigger Decompile\disasm\notes\menu_sfx.md`)*

Companion to `disasm/notes/naming_screen_sfx.md` (the mailbox, the stager C2/EB03 and the $0D76
throttle are derived there and are NOT re-derived here). This dossier covers ROM **mode-3 screens
$00 (ribbon), $01 (Equip/Status), $02 (Item), $03 (Tech), $04 (Config), $05 (Form), $06 (Save)** —
i.e. exactly the screens our `menuPhase` machine reaches — plus the mode-3 **open prelude** that runs
before screen $00 appears.

Scope statement up front: our engine's menu is **completely silent**. Everything below is what the
ROM posts, where, and under which branch. Every row carries its ROM address and, where it could be
driven, verbatim oracle output.

---

## 0. The machinery (recap, one paragraph, all re-read here)

Mailbox `$7E:1E00`=cmd `$1E01`=id `$1E02`=pan `$1E03`=param2; one `JSL $C7:0004` ships it.

**The stager C2/EB03** — raw bytes re-read from the ROM in this pass,
`d[0x2EB03:0x2EB1F]`:

```
A2 18        EB03  LDX #18          ; X is 8-BIT here (EAC3 SEP #30)
8E 00 1E     EB05  STX $1E00        ; cmd = $18
8D 01 1E     EB08  STA $1E01        ; id = A          <-- STAGED even when throttled
AD 76 0D     EB0B  LDA $0D76
D0 0E        EB0E  BNE ->EB1E       ; THROTTLED: no pan write, no JSL
A9 03        EB10  LDA #03
8D 76 0D     EB12  STA $0D76        ; re-arm 3
A9 80        EB15  LDA #80
8D 02 1E     EB17  STA $1E02        ; pan = $80
22 04 00 C7  EB1A  JSL $C7:0004     ; SHIP
60           EB1E  RTS
```

Drain, `d[0x2845E:0x28468]` = `AD 76 0D F0 03 CE 76 0D` = `C2/845E LDA $0D76 / BEQ ->8466 /
C2/8463 DEC $0D76`, inside the **mode-3 NMI** — it runs once per frame for as long as the mode-3
shell is up, i.e. from the prelude onwards, and **not** during the C0-side entry fade.

Wrappers (raw bytes re-read):

| wrapper | bytes | id | sites |
|---|---|---|---|
| C2/EABA | `08 E2 30 20 03 EB 28 60` | **id already in A** | 4 JSR: 9CDE D635 D6AA D7AF |
| C2/EAC2 | `08 E2 30 A9 00 20 03 EB 28 60` | **$00** — the generic cursor blip | 89 JSR + **3 JMP (9AAA A45A BFD1)** |
| C2/EACC | `08 E2 30 A9 01 20 03 EB 28 60` | **$01** — the reject buzz | 3 JSR (B1BA B23D E3FB) + **11 JMP (9C8D B38F B3EF B406 B437 B4E3 C63A CEF2 D5D6 D686 D738)** |
| C2/EAD6 | `08 E2 30 9C 76 0D AD BC 0D 29 …` | — | **DEAD, 0 callers** |
| C2/EAF9 | `A2 19 8E 00 1E 9C 76 0D 80 05` | id in A, **cmd $19**, `STZ $0D76` first ⇒ **can never be throttled** | 1 JSR (9047) |

> **The `20 C2 EA` scan is not the census.** Three EAC2 sites and eleven EACC sites are `JMP`-form,
> and **two menu SFX write the mailbox inline and use no wrapper at all**: C2/CF37 (the save jingle)
> and C2/6BD8. Complete bank-C2 mailbox census (my own scan): **17 `STA $1E00`** at
> `24E5 24F6 2643 2EEB 2F3C 6BDC 80AC 80B8 80CB 80D4 80E7 C02A CBD0 CBF9 CC3B CF39 E696`,
> **2 `STX $1E00`** at `EAFB EB05`, zero STY, zero STZ.

Dispatcher C2/8270 → screen table `d[0x28292:0x282B2]` =
`CA 99 | 29 9C | 34 AA | 53 B3 | 35 BF | 21 C6 | D4 CE | A6 C7 | 91 82 | 91 82 | 91 82 | F0 D4 |
19 D5 | E3 E1 | 0B E6 | 0A FE` → 00=99CA 01=9C29 02=AA34 03=B353 04=BF35 05=C621 06=CED4 07=C7A6
0B=D4F0 0C=D519 0D=E1E3(naming) 0E=E60B 0F=FE0A. `$67`=screen, `$68`=sub-state, **X/Y are 16-bit
inside a handler while A is 8** (C2/827D `REP #30`, C2/828C `SEP #20`) — disassemble with `m=1 x=0`.

Two shared input bytes used by every gate below: **`$0D1D` bit7 = confirm (A), bit6 = cancel (B),
bit1 = R, bit0 = L** (C2/E984: `2E9BC LDA #02 / TSB $0D1D` for R, `2E9C8 LDA #01 / TSB $0D1D` for L,
`2E9D2 LDA #40 / TSB $0D1D` from `$0D83`), and **`$81` = the PRE-move snapshot of the cursor `$54`**
(C2/E9E5 `LDA $54` / C2/E9E7 `STA $81`).

---

## 1. THE TABLE

Legend — **confirmed-by**: `[dis]` ROM listing/raw bytes only; `[orc]` driven on `build/ct.exe`;
`[dis+orc]` both. **thr** = goes through the C2/EB03 throttle (`y`) or ships unconditionally (`n`).
`—` in cmd/id = the action makes **no sound**, and that silence is a measured/derived result.

### 1.1 OPEN — `src/field.c:7688-7706` (menuPhase 0 → 1) and `:7712-7743` (the fade)

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| A1 | X pressed on the field | C0/1916 `LDA $00F6` / C0/1923 `BIT #40` / C0/1925 `BNE ->192F`; gates C0/192F `LDA $1F BNE`, C0/1934 `LDA $62 BEQ`, C0/1939 `LDA $10 BEQ` | — | — | the button handler itself posts nothing | n | [dis+orc] |
| A2 | the brightness fade-out (15 frames) | C0/1943 `LDA $19 / BEQ->1958 / BMI->1958 / 1949 DEC $19 / 194D JSL FD/C2C1 / 1953 JSR C0/EC60 / 1956 BRA ->193E` | — | — | **SILENT**: bank $FD contains **zero** `JSL $C7:0004` (byte scan `22 04 00 c7` over the 4 MB: per-bank counts `{C0:22, C1:6, C2:18, C3:3, CD:6, FF:4}`), and no bank-C0 post site lies in the loop | n | [dis+orc] — the open trace shows zero mailbox activity from f0 to f19 |
| A3 | mode-3 entry → **the OPEN PRELUDE, 5 posts** | C0/1958 → C0/195E `TDC` / C0/195F `XBA` (A = **kind $01**) → C0/1960 `JSL $C2:8000` → C2/803D `LDA $0A00` / C2/8040 `BNE ->8064` → C2/8064 → C2/8068 `JSR C2/8096` | see **§2** | see §2 | unconditional; kind 1 → request table `d[0x2808F:0x28096]` = `0E 00 0C 0D 0F 07 0B` → screen **$00** | **n** (the prelude never touches $0D76) | [dis+orc] |
| A4 | the ribbon's own sub-state 0 (the screen appearing) | C2/99CA[0] → C2/99CE… C2/9A16 `INC $68` / C2/9A18 `JMP C2/83B2` | — | — | **SILENT, and it is the ONLY sub-state-0 that is**: `d[0x299CE:0x299D6]` = `64 7f 64 80 a5 75 d0 03` — no `20 c2 ea`. Compare C2/9C31 = `20 c2 ea …`, C2/AA40 = `20 c2 ea …`, C2/B35D = `20 c2 ea …`, C2/BF3D = `20 c2 ea …` | n | [dis+orc] — 124 frames of post-open idle, zero mailbox change |

**Correction carried from the skeptic pass:** row A1's operands `$1F/$62/$10/$19` are DIRECT-PAGE
with **D = $0100** in that routine, so the real bytes are **`$011F` (explore), `$0162` (cutscene
lock), `$0110`, `$0119` (brightness)**. Proof the DP is $0100: C0/195E `TDC` / C0/195F `XBA` yields
A = the HIGH byte of D, and the measured kind is `$7E0A00 = 01` (writer `C2:8020`). `src/field.c:
7689-7691` already gates on those bytes.

### 1.2 RIBBON, screen $00 — `src/field.c:7771-7819`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| R1 | **D-pad LEFT** moves the tab (wrap 0→5) | C2/9A20 `JSR C2/E984` → C2/E9E2 → C2/E9FE `STX $0D1E`(=1) → C2/EA17 `JSR ($EA1F,X)` → C2/EA40 `LDA $9384,Y` → C2/EA48 `STA $54` → back at **C2/9A2C `CMP $0411` / 9A2F `BNE ->9A45`** → C2/9A45 `STA $0411` → **C2/9A73 `JSR C2/EAC2`** | 18 | 00 | fires **only** if the nav table produced a different tab | y | [dis+orc] `f7 7E0411=05*(C2:9A48) 7E0D1E=01*(C2:EA01) 7E0054=05*(C2:EA4A)` then `f9 7E1E01=00*(C2:EB0B) 7E1E02=80*(C2:EB1A) 7E0D76=03*(C2:EB15)` |
| R2 | **D-pad RIGHT** moves the tab (wrap 5→0) | same, via C2/EA45 `LDA $9385,Y` | 18 | 00 | same C2/9A2C compare | y | [dis+orc] `f7 7E0411=01*(C2:9A48) 7E0D1E=00*(C2:EA01)`; `f9 … 7E0D76=03*(C2:EB15)` |
| R3 | **D-pad UP / DOWN** on the ribbon | C2/EA36 `LDA $9382,Y` / C2/EA3B `LDA $9383,Y` → C2/9A2C compare **EQUAL** → RTS | — | — | **SILENT BY DATA, not by code.** The nav table lives in WRAM at `$7E:9382 + cursor*6`; measured: `0000050100E3 0101000200E3 0202010300E3 0303020400E3 0404030500E3 050504000000` — bytes [+0] (Up) and [+1] (Down) of every 6-byte record **equal the record index**, so `$54` never differs from `$0411` | n | [dis+orc] Up/Down set `$0D1E` but leave the mailbox untouched |
| R4 | **HOLD** a direction (auto-repeat) | R1/R2 re-entered per repeat | 18 | 00 | cadence measured: press at f7, **first repeat +32 frames**, then **every 5 frames**. 5 > the 3-frame $0D76 window ⇒ **no ribbon repeat is ever swallowed** | y | [orc] `--input "0:6,40:110"` → `f7 $0411=05*`, `f9 $0D76=03*`, f39/f44/f49/f54/f59/f64/f69/f74… each with its own `$0D76=03*(C2:EB15)`, and the drain visible 3→2→1→0 at `C2:8466` **⚠ the batch's first citation for this row used `40:1 x100`, which this repo's drive grammar does NOT expand — it produced nothing. Re-measured with a literal hold.** |
| R5 | **L or R SHOULDER** | C2/E9BC `LDA #02 / TSB $0D1D`, C2/E9C8 `LDA #01 / TSB $0D1D` | — | — | **SILENT**: `C2/9A23 BIT $0D1D` tests only BMI (bit7) and BVS (bit6); bits 1/0 are dead on screen 0 | n | [dis+orc] |
| R6 | **A** on the ribbon (opens a sub-screen) | C2/9A26 `BMI ->9A77`; C2/9A77 `LDX #BD51 / JSR C2/ED31 / 9A7D LDA $0411 / INC A / 9A81 STA $67 / 9A83 STZ $68 / … / 9A94 RTS` | — | — | **the A press itself is SILENT** — the blip you hear belongs to the **destination screen's sub-state 0, one frame later** (rows E1/I1/T1/C1/F1/S1). ⚠ the gate is established by **measurement**, not by a range scan: C2/9A7A is `JSR C2/ED31`, a command-list interpreter ending `2ED50 JSR ($ED59,X)` — an indirect dispatch a scan cannot see through | n | [orc] on the frame `$0067` changes (writer `C2:9A83`) there is no `C7:0196` consume and `$0D76` stays 00 |
| R7 | **B** on the ribbon — CLOSES the menu | C2/99CA sub-state 1 | — | — | **TOTALLY SILENT** | n | [orc] 280 frames after the open: the only writer block in the whole run is the prelude at f24; the fade-out `$0119=01..0F` runs with zero mailbox activity |
| R8 | ribbon idle / the gold+time redraw | — | — | — | SILENT | n | [orc] 124 idle frames, no change |

### 1.3 SUB-SCREEN ENTRY (the blip that belongs to A, one frame late)

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| E1 | Equip/Status opens (screen 1 sub 0) | C2/9C31 `JSR C2/EAC2 / INC $68 / JMP C2/A11A` | 18 | 00 | unconditional, first instruction | y | [dis+orc] `f48 7E0067=0100*(C2:8211)` then `f49 7E1E00=00008005(C7:0196) 7E0D76=03*(C2:EB15)` |
| I1 | Item opens (screen 2 sub 0) | C2/AA40 `JSR C2/EAC2 / LDA #02 / STA $68 / JMP C2/ADD0`; sub-table `d[0x2AA34:0x2AA40]` = `40 AA 4A AA 86 AA C8 AA 4A AB 1D AC` | 18 | 00 | unconditional. Screen 2 has **no entry gate** | y | [dis+orc] `f89 $0067=02*(C2:9A83) $0D76=00` → `f90 $0D76=03*(C2:EB15) $0054=00*(C2:AE0A)` |
| T1 | Tech opens (screen 3 sub 0) | C2/B35D `JSR C2/EAC2 / INC $68 / JMP C2/B979`; sub-table `d[0x2B353:0x2B35D]` = `5D B3 65 B3 6D B3 AE B3 8E B4` | 18 | 00 | unconditional | y | [dis+orc] |
| C1 | Config opens (screen 4 sub 0) | C2/BF3D `JSR C2/EAC2 / INC $68 / JMP C2/C3B5`; sub-table `d[0x2BF35:0x2BF3D]` = `3D BF 45 BF 6D BF 9F BF` | 18 | 00 | unconditional | y | [dis+orc] `f85 $0067=04*(C2:9A83)` / `f86 $0054=00*(C2:C3BD) $1E00=00008005(C7:0196) $0D76=03*(C2:EB15)` |
| F1 | **Form opens — gate PASSES** | sub-table `d[0x2C621:0x2C627]` = `27 C6 5D C6 B9 C6`; C2/C627 raw = `E2 30 / A6 7F / AD 0B 0F / 18 / 6D 0C 0F / C9 02 / B0 07 / 64 67 / E6 68 / 4C CC EA / 20 49 C6 / C2 10 / 20 C2 EA / 4C F3 CA` | 18 | 00 | **C2/C634 `BCS ->C63D` TAKEN**, i.e. `($0F0B + $0F0C) >= 2`. Post at **C2/C642** | y | [dis] — no repo state passes this gate |
| F2 | **Form opens — gate REFUSES → bounce to ribbon.** THE REFUSAL SOUNDS | C2/C636 `STZ $67` / C2/C638 `INC $68` / **C2/C63A `JMP C2/EACC`** | 18 | **01** | `BCS` NOT taken: `($0F0B + $0F0C) < 2` | y | [dis+orc] on every field state this repo owns (`$0F0B/$0F0C = 00/00`) → `$1E01` flips 00→01 |
| S1 | **Save opens — gate PASSES** | C2/CED4 sub-table `DC CE FD CE 61 CF 92 CF`; C2/CEDC `LDA $0D34 / ORA $0294 / ORA $0299 / ORA $029E / AND #40 / CEEA BEQ ->CEF5` → **C2/CEF5 `JSR C2/EAC2`** / CEF8 `INC $68` / CEFA `JMP C2/D0E5` | 18 | 00 | **C2/CEEA BEQ TAKEN**. `$0D34` is built at menu-open by C2/9875: `9898 STZ $0D34`, `98A1 LDA $0100 / 98A4 CMP #01F0 / 98A7 BCS ->98B5` (overworld skips), `98A9 DEC $0D32`, `98AC LDA $7F01CE / 98B0 BMI ->98B5` (save point), else `98B2 DEC $0D34` → $FFFF | y | [dis+orc] `saves/ow_w0.state`: `f143 $0067=06*(C2:9A83) $0D76=00` → `f144 $0054=00*(C2:D0EC) $0D76=03*(C2:EB15)` |
| S2 | **Save opens — gate REFUSES → bounce to ribbon.** THE REFUSAL SOUNDS | C2/CEEC `STZ $67` / CEEE `LDA #01 STA $68` / **C2/CEF2 `JMP C2/EACC`** | 18 | **01** | `BEQ` NOT taken: low byte of `$0D34|$0294|$0299|$029E` has bit6 set (field map, no save point ⇒ `$0D34=FFFF`) | y | [dis+orc] `out_s8.state` ($0100=0008, $7F01CE=0001): `f144 7E1E00=00018005*(C7:0196) 7E0067=00*(C2:CEEE) 7E0D76=03*(C2:EB15)` |

### 1.4 EQUIP/STATUS (screen 1) and TECH (screen 3) — `src/field.c:7820-7844`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| E2 | char-select **Up/Down, cursor MOVED** | C2/9D17 → C2/9D40 `JSR C2/9E76 / LDA $54 / STA $0412 / CLC / ADC $0413 / STA $71 / 9D4E CMP $83 / STA $83 / 9D52 BEQ ->9D57 / 9D54 JSR C2/EAC2` | 18 | 00 | `C2/9D52 BEQ` — post only when `($0412+$0413)` differs from the previous composite `$83` | y | [dis+orc] `f133 $0054=01*` / `f175 =02*` / `f217 =01*`, one EAC2 each |
| E3 | char-select Up/Down **REFUSED** (one-character roster; the cursor graph maps the cell to itself) | C2/9D52 branch TAKEN | — | — | there is **no "occupied slot" test** in the ROM here: the list LENGTH is `$85` (`9D2A CMP $85 / BCS`, `9D34 BNE / 9D38 LDX $85 / DEX`) and the move is the shared cursor graph | n | [dis+orc] `saves/solo_forest.state` (`$0085=01`): three presses, **no** `$0D76`/`$1E00` row at all |
| E4 | char-select **confirm — A (`$0D1D` bit7) OR D-pad RIGHT (`$5A` bit0)** | C2/9C3C `LDA $5A / AND #01 / 9C40 BNE ->9C4C / BIT $0D1D / 9C45 BMI ->9C4C / 9C47 BVS ->9C6D`; **C2/9C52 `JSR C2/EAC2`** | 18 | 00 | either arm | y | [dis+orc] A: `f90 $0083=00FF*(C2:9C50) $0D76=03*(C2:EB15)`. RIGHT (serial 0x80): same shape |
| E5 | …**and a SECOND blip** when the equip panel finishes opening | C2/9C5B `JSR C2/821E` (the mode-3 coroutine YIELD) → **C2/9C5E `JSR C2/EAC2`** | 18 | 00 | no gate of its own; the continuation after the yield | y (irrelevant — 25 frames later) | [dis+orc] blips at f90 (9C52) and f115 (9C5E), **25 frames apart**; a ONE-FRAME press still yields both |
| E6 | char-select **B → ribbon** | C2/9C47 `BVS ->9C6D` → C2/9C6D `JMP C2/9A98` → C2/9A98 `SEP #20 / REP #10 / STZ $67 / LDA #01 STA $68 / LDA #74 / JSR C2/8255 / JSR C2/9B71 / **9AAA JMP C2/EAC2**` | 18 | 00 | this is **the shared back-to-ribbon exit of every sub-screen**, and it is `4C C2 EA` — invisible to the `20 C2 EA` scan | y | [dis+orc] `f113 $0067=0000*`, `W 7E1E01 <- 00 @C2/EB0B` |
| E7 | equip detail **Up/Down over the 4 gear slots, MOVED** | C2/9ECD → 9ED0 `JSR C2/A38B` / 9ED3 `JSR C2/A00B` / 9ED6 `LDA $54 / 9ED8 CMP $0FC5 / STA $0FC5 / 9EDE BEQ ->9EE3 / 9EE0 JSR C2/EAC2` | 18 | 00 | `C2/9EDE BEQ` — only when the slot cursor changed | y | [dis+orc] `f152 $0FC5=0100*(C2:9EDE) $0D76=03*` / `f174 $0FC5=0200*` |
| E8 | equip detail **L/R switches the displayed CHARACTER** | C2/A00B `LDX $71 / LDA $0D1D / AND #03 / A015 BEQ ->A04F / BIT #02 / BNE ->A022 / DEX … / A029 CPX $71 / STX $71 / STX $0412 / A030 BEQ ->A04F / … / **A043 JSR C2/EAC2**` | 18 | 00 | two gates: `$0D1D & 3 != 0`, and the index actually changed | y | [dis+orc] `f196 $0412=0100*(C2:A035) $0D76=03*` / `f218 $0412=0000*` |
| E9 | equip detail **L/R REFUSED** (single-character roster `$85==1`) | C2/A030 BEQ TAKEN | — | — | with `$85==1` both arms land back on `X == $71` (R: `INX`→1, `CPX $85` fails, `LDX #00`; L: `DEX`→$FF, BPL fails, `LDX $85 / DEX / DEX`→$FF, `INX`→0) | n | [dis+orc] |
| E10 | equip detail **A REFUSED — nothing equippable in the bag** | C2/9C83 `LDA $0077 / STA $54 / 9C88 LDA $0FC6 / 9C8B BNE ->9C90 / **9C8D JMP C2/EACC**` | 18 | **01** | `$0FC6 == 0`. `$0FC6` is built by C2/8953: `STZ $0FC6 / LDA $0077 / ASL / TAX / LDA $C289DC,X → $00 / LDA $C289DD,X → $01 / LDY #00 / LDA $2400,Y / BEQ / CMP $00 BCC / CMP $01 BCS / … INC $0FC6 / CPY #F2` — a count of bag items whose id falls in the slot's window | y | [dis+orc] `f240 7E1E00=00018005*(C7:0196) $0054=02(C2:9C88) $0D76=03*(C2:EB15)` |
| E11 | equip detail **A ACCEPTED → open the gear list** | C2/9C90 `JSR C2/EAC2 / INC $68 / 9C95 JMP C2/A41A` | 18 | 00 | `$0FC6 != 0` | y | **[dis] ONLY** — unreachable, see §4 |
| E12 | …**and a SECOND blip** when the gear list finishes sliding in | C2/A41A → **C2/A45A `JMP C2/EAC2`** (another JMP-form site) | 18 | 00 | unconditional tail of C2/A41A | n (≈7-8 frames later) | **[dis] ONLY.** Derived, not analogised: C2/A4AF seeds `$26 = 0004`, `STZ $96A3`; C2/A518 does `$96A3 += $26`; the loop `A507 JSR A518 / LDA $96A3 / CMP #20 / BCS ->A516 / A511 JSR C2/821E / BRA ->A507` = 8 iterations, **7 yields** ⇒ 7-8 frames |
| E13 | equip detail **B (or D-pad LEFT, `$5A` bit1) → char-select** | C2/9C73 `LDA $5A / AND #02 / 9C77 BNE ->9C98 / BIT $0D1D / 9C7C BMI ->9C83 / 9C7E BVS ->9C98`; C2/9C98 `LDA #01 STA $68 / **9C9C JSR C2/EAC2**` | 18 | 00 | either arm | y | [dis+orc] `f282 7E1E00=00008005*(C7:0196) $0D76=03*(C2:EB15)` |
| E14 | gear list **Up/Down, cursor moved** | C2/9CA5 → C2/9CAF `JSR C2/9F05` / `JSR C2/A5C3` / `9CB7 LDA $81 / CMP $54 / 9CBB BEQ ->9CC0 / **9CBD JSR C2/EAC2**` | 18 | 00 | `$81` vs `$54`. ⚠ `$81` has a **second writer**, `C2/9403 STA $81` (#FF) inside C2/93BF, reached on any page change | y | **[dis] ONLY** — unreachable |
| E15 | gear list **L/R page flip** | C2/9CAF → C2/9F05 → `9F10 CMP #00 / 9F12 BEQ ->9F2F / **9F26 JSR C2/EAC2**` | 18 | 00 | **CORRECTED**: C2/93BF returns the **new page value in A**, not a moved-flag. A flip whose new page is **0** (the backward clamp `93D9 LDA $0DDB / STA $54 / 93DE LDA #00 / BRA ->93F8`) still stores the page and still forces `9403 LDA #FF / STA $81`, but returns A=0 ⇒ 9F26 is skipped and **the blip is emitted one call later at C2/9CBD**. On a successful non-zero flip **both** 9F26 and 9CBD run in the same frame and the second is swallowed | y | **[dis] ONLY** — unreachable |
| E16 | **EQUIP COMMIT** (A on an item in the gear list) | C2/9CC1… `9CD9 JSR C2/87D5 / 9CDC LDA #B5 / **9CDE JSR C2/EABA**` then `9CE1 JSR C2/9CFC / 9CE4 JSR C2/EAC2` | 18 | **B5** | id in A. The **very next** call tries id $00 in the SAME frame and is **swallowed** by the `$0D76=3` this post just armed — only $B5 is heard | y | **[dis]** + wrapper contract measured: `--callrom C2/EABA --rega B5 --writes` → `w 001E00 = 18 (by C2:EB08)` / `w 001E01 = B5 (by C2:EB0B)`. **The play path was never reached in ~9000 frames of menu mashing** (see §4) |
| E17 | TECH screen L/R over `$0418` | C2/B353 sub-table `5D B3 65 B3 6D B3 AE B3 8E B4`; sites C2/B386 B392 B3C5 B43A B49C | 18 | 00 | — | y | **⚠ the source batch's row is TRUNCATED mid-citation in the material handed to me; everything legible was confirmed by disassembly and by a drive, but I will not restate a gate I cannot quote in full.** See §4 |
| E18 | TECH refusals | five `JMP C2/EACC` sites inside screen 3: C2/B38F B3EF B406 B437 B4E3 | 18 | **01** | per-site, **not individually derived here** | y | [dis] site census only — see §4 |

### 1.5 ITEM (screen 2) — `src/field.c:7845-7902`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| I2 | list **Up/Down inside the 10-row window** (incl. over EMPTY slots) | C2/AA86 / C2/AAC8 → `JMP C2/AC3A` → `AC54 JSR C2/EA53 / AC57 BMI ->AC6E / AC59 JSR C2/AD2E / AC5C LDA $54 / STA $0415 / AC61 CMP $81 / AC63 BEQ ->AC68 / **AC65 JSR C2/EAC2**` | 18 | 00 | `$54` vs the pre-move `$81` | y | [dis+orc] `f116 $0D76=03*(C2:EB15) $0054=01*(C2:EA4A) $0415=01*(C2:AC61)`, then f138, f160 |
| I3 | list at a window edge → the 242-slot window **SCROLLS** (`$0416 ±1`) | C2/AC57 `BMI ->AC6E` → `AC6E LDA $54 / CLC / ADC $0416 / DEC A / AC75 CMP #F0 / AC77 BCS ->AC59` → `AC93 DEC $0416` / `AC9E INC $0416` → `ACB0 JSR C2/AE65 / ACB3 LDX #FBE3 / JSR C2/8385 / **ACB9 JSR C2/EAC2**` | 18 | 00 | **no gate on the blip**; the PATH is gated by `CMP #F0 / BCS` (absolute slot must stay < 240). The separate row blip at AC65 is suppressed here because `$54 == $81` (C2/ACEC `JSR C2/AC59` re-enters that code as a subroutine) | y | [dis+orc] `f278/f296/f314 $0D76=03*(C2:EB15) $0054=09 $0416=01*/02*/03*(C2:ACA1)` |
| I4 | **Up at row 0/scroll 0**, or **Down at absolute slot 240** | AC75 `CMP #F0` / AC77 `BCS ->AC59` then AC61/AC63 `BEQ` | — | — | at row0/scroll0 the computed byte is `$FF >= $F0`; `$54` did not move | n | [dis+orc] two Up presses produce **no monitor row at all** |
| I5 | **A picks the slot under the cursor — INCLUDING AN EMPTY SLOT** | C2/AA8F `BIT $0D1D` / AA92 `BMI ->AA99` → **C2/AA99 `JSR C2/EAC2`** / AA9C `LDX $0D26 / STX $0F00 / INC $68` | 18 | 00 | **NONE.** There is no emptiness test: an empty slot is picked and blips exactly like an occupied one, and picking sounds **identical** to moving the cursor | y | [dis+orc] `f148 $0D76=03*(C2:EB15) $0F00=0100*(C2:AAA2)` on an EMPTY row |
| I6 | picked: **A on a DIFFERENT slot = the MOVE swap** | C2/AACE `BMI ->AAD5` → **C2/AAD5 `JSR C2/EAC2`** → AAD8 `LDX $0F00 / AADB CPX $0D26 / AADE BEQ ->AB14`; swap body AAE0-AAFE | 18 | 00 | none on the blip (it precedes the same/different test). **The swap body contains NO sound site — the MOVE makes exactly ONE sound** | y | [dis+orc] `f180 $0D76=03*(C2:EB15) $7E2400=00D6*(C2:AAF1)` |
| I7 | picked: **B cancels the pick** | C2/AAD0 `BVS ->AAFF` — branches **OVER** the AAD5 sound site; AAFF..AB13 has none | — | — | **SILENT**, and it is the screen's one asymmetry: B on the *list* DOES blip (I9), B cancelling a *pick* does not | n | [dis+orc] `f150 $1E01=00 $0D76=00 $0F00=FFFF*(C2:AA8C)` = no post; the NEXT B (on the list) does blip |
| I8 | picked: **A on the SAME slot** (USE / deselect) | C2/AADE `BEQ ->AB14`; C2/AB14 `LDX $0F00 / LDA $2400,X / STA $04C9 / TAY / LDA $7700,Y / AND #02 / **AB24 BNE ->AB3E** / LDA $04C9 / BEQ ->AB3A / CMP #BC / BCS ->AB3A / DEC $0D9A / LDA #05 STA $68 …` ; `AB3E JSR C2/B0AB / LDA #02 STA $68` | 18 | 00 | the A blip at AAD5 already fired. **CORRECTED (the source row said `BEQ`; the byte at 0x2AB24 is `D0 18` = BNE, and the row contradicted itself).** Corrected semantics: **bit1 of `$7E:7700+id` SET → the item ENTERS the use flow** (C2/B0AB → the C2/B17F target loop; measured `$77BD` (Tonic) `= 02`). bit1 CLEAR falls through to the id tests; **id < $BC → sub-state 5**; **id >= $BC → straight back to the list**. The silent-refusal set is exactly `{$04C9 == 0}` ∪ `{bit1 clear AND id >= $BC}` — **and neither arm buzzes** | y | [dis+orc] for id `$D6`: one throttled cmd-18 id-00 blip, then back to the list, no buzz |
| I9 | list **B → the Use/Move + Organize label row** | C2/AA94 `BVS ->AAAD` → **C2/AAAD `JSR C2/EAC2`** / AAB3 `LDA #01 STA $68` / AABA `LDA #0A STA $54` | 18 | 00 | none | y | [dis+orc] `f190 $0D76=03*(C2:EB15) $0054=0A*(C2:AABE)` |
| I10 | label row **L/R toggles Use-Move ↔ Organize** | C2/AA4A `JSR C2/E984 / BIT $0D1D / BMI ->AA5E / BVS ->AA83 / LDA $81 / CMP $54 / BEQ ->AA5D / **AA5E JSR C2/EAC2**` (raw `20 84 e9 2c 1d 0d 30 0c 70 2f a5 81 c5 54 f0 03 20 c2 ea 60`) | 18 | 00 | `$81` vs `$54` | y | [dis] + site census |
| I11 | label row **B → ribbon** | C2/AA83 `JMP C2/9A98` (raw `4c 98 9a`) → C2/9AAA | 18 | 00 | the shared back-path | y | [dis+orc] |
| I12 | **Organize / the sort itself** | C2/AB4A `JSR C2/AB62 / JSR C2/821E / JSR C2/AE43 / LDX #FBE3 / JSR C2/8385 / LDA #0A STA $54 / LDA #01 STA $68 / RTS` | — | — | **SILENT.** Byte scan of `0x2AB4A..0x2ABE8` for any wrapper call or `STA $1E0x`: **zero hits** | n | [dis] |
| I13 | **USE target-select: cursor moved** | C2/B17F loop → **C2/B288 `JSR C2/EAC2`** | 18 | 00 | `CMP $0DBE / BEQ ->B28B` | y | [dis] site census |
| I14 | **USE target-select: A on an invalid target = THE SCREEN'S ONLY BUZZ** | **C2/B1BA `JSR C2/EACC`** and its twin **C2/B23D `JSR C2/EACC`** | 18 | **01** | `$0DBC == 0` | y | [dis] — the two EACC sites in the item region; **not driven** (no repo state has a usable-item target that fails) |
| I15 | **USE target-select: B leaves** | **C2/B1C9 `JSR C2/EAC2`** | 18 | 00 | — | y | [dis] |

Byte scan over the whole item region `0x2AA34..0x2B353` for wrapper calls and direct mailbox stores
reproduces exactly: `AA40 AA5A AA5E AA99 AAAD AAD5 AC2A AC65 ACB9 → EAC2`, `B1BA B23D → EACC`,
`B1C9 B288 → EAC2`, **direct mailbox stores in range: NONE**.

### 1.6 CONFIG (screen 4) — `src/field.c:7903-7910`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| G1 | cursor **MOVED** (sub-state 1, any direction that changes the cell) | C2/BF45 → `BF4A JSR C2/E984 / BF4D BIT $0D1D / BF50 BVS ->BF5F / BF52 JSR C2/BFD4 / BF55 LDA $81 / BF57 CMP $54 / BF59 BEQ ->BF5E / **BF5B JSR C2/EAC2**` | 18 | 00 | `$81` vs `$54` | y | [dis+orc] Left 0→5 `f107 $0054=05*(C2:EA4A) $0417=05*(C2:BFDD) $0D76=03*(C2:EB15)`, then f117/f127/f137/f147/f157 — 5 moves, 5 posts |
| G2 | **A pressed on ANY cell** (sub-state 1) | C2/BFD4 `PHP / SEP #20 / LDA $54 / STA $0417 / ASL / TAX / PHP / BFE0 JSR ($BFFF,X) / PLP / … / BFF5 LDA $0D1D / BFF8 BPL ->BFFD / **BFFA JSR C2/EAC2**` | 18 | 00 | `$0D1D` **bit7 only** (B does not reach it). Fires for **every** cell $00..$0B whether or not the cell handler changed anything | y | [dis+orc] `f122 $0054=01 $1E00=00008005(C7:0196) $0D76=03*(C2:EB15)` |
| G3 | **cell 0 A pressed → a RAW DRIVER COMMAND, not an SFX** | C2/C017 `LDA $0D1D / BPL ->C037 / LDA $0D86 / INC A / AND #01 / STA $0D86 / STA $0F09 / C028 LDA #F3 / STA $1E00 / LDA $0D86 / STA $1E01 / **C033 JSL $C7:0004**` | **F3** | **`$0D86` after the toggle (00/01)** | A only. **`$1E02`/`$1E03` are NEVER written by this path — the bytes shipped to $2142/$2143 are STALE** | n | [orc] `--callrom C2/C01C --writes` → `w 7E0D86 = 01 (by C2:C025)` / `w 7E0F09 = 01` / `w 7E1E00 = F3 (by C2:C02D)` / `w 7E1E01 = 01 (by C2:C033)` / `w 002143 = 05 (by C7:0623)` / `w 002142 = C8` / `w 002141 = 01` / `w 002140 = F3 (by C7:0635)` |
| G4 | **cells $0A/$0B (the two sliders): d-pad LEFT/RIGHT adjusts `$0D8B`/`$0D90` (±1 mod 8)** | C2/C0B9 `LDX #0D8B / JSR C2/C164` and C2/C0D1 `LDX #0D90 / JSR C2/C164`; C2/C164 `TDC / LDA $0000,X / TAY / C169 LDA $5A / AND #03 / C16D BEQ ->C182 / AND #01 / BNE ->C175 / DEY / DEY / C175 INY / TYA / AND #07 / STA $0000,X / C17C JSR C2/C456 / **C17F JSR C2/EAC2**` | 18 | 00 | `$5A & 3` (bit0 Right, bit1 Left). **No value-changed test** — the `AND #07` wrap always changes the value, so every accepted L/R blips | y | [dis+orc] `f133/f149 $0D8B=05*/04*(C2:C17C) $0D76=03*(C2:EB15)` **with `$0054` UNCHANGED** |
| G5 | **cell 6 while `$0F0F != 0`: RIGHT/LEFT** | C2/C12C `LDA $0F0F / BEQ ->C140 / C131 LDX #0D8C / C134 JSR C2/C164` → C2/C17F | 18 | 00 | same `$5A & 3` | y | [dis+orc] `f183 $0054=06 $0F0F=01 $0D8C=01*(C2:C17C) $0D1F=01 $0D76=03*(C2:EB15)` |
| G6 | **cell 6: A or B toggles the `$0F0F` sub-mode** (`$93A4 := $3B38 / $3788`) | C2/C140 `LDA $0D1D / AND #C0 / C145 BEQ ->C15D / LDX #3B38 / LDA $0F0F / EOR #01 / STA $0F0F / BEQ ->C157 / LDX #3788 / C157 STX $93A4 / **C15A JSR C2/EAC2**` / C15D `LDA $0F0F / C160 STA $0D1F` | 18 | 00 | **A or B**. On A, the later C2/BFFA is **swallowed** by the `$0D76=3` this post just armed | y | [dis+orc] `f133 $0F0F=01*(C2:C152) $0D1F=01*(C2:C163) $0D76=03*(C2:EB15) $93A4=8837*(C2:C15A)`; `f172 $0F0F=00* $93A4=383B* $0067=04` (**B did NOT exit the screen**) |
| G7 | **cell 6 while `$0F0F != 0`: UP or DOWN** | `C2/C160 STA $0D1F` suppresses the neighbour move (C2/EA01 `LDA $0D1F / EA04 BNE ->EA1A`); C2/BF59 BEQ then skips BF5B | — | — | **SILENT — and this is the ONLY genuine instance of "a direction that changes nothing".** ⚠ **REFUTED CLAIM CARRIED:** the source batch generalised this silence to cells $0A/$0B and to L/R; both are **audible** (G4/G5), and `$0D1F` is written **only** inside the cell-6 handler — on the sliders `$0D1F` measured **00** while `$0054` still failed to move (the suppression there is the neighbour TABLE, not `$0D1F`) | n | [orc] Up and Down on cell 6 produce **no monitor row at all** |
| G8 | **cells $0A/$0B: UP or DOWN** | the neighbour table moves `$54` between $0A and $0B → C2/BF5B | 18 | 00 | ⚠ **REFUTED CLAIM CARRIED:** the source batch called "no L/R that frame" silent. It is silent **only** if there is also no Up/Down and no A/B | y | [orc] `f163 $0054=0B*(C2:EA4A) $0417=0B*(C2:BFDD) $0D76=03*(C2:EB15)`, f177, f191 |
| G9 | **cells $0A/$0B: A or B leaves the slider back to cell 5** | C2/C0BF (C2/C0D7) `LDA $0D1D / AND #C0 / BEQ ->C0D0 / LDA #05 / STA $54 / STA $0417 / STZ $0F0E` | 18 | 00 | on **B**, C2/BFF8 `BPL` skips BFFA so **C2/BF5B** is the post (`$81=$0B != $54=$05`); on **A**, BFFA posts first and BF5B is swallowed | y | [dis+orc] `f172 $0054=05*(C2:C0E2) $0417=05*(C2:C0E5) $0F0E=00*(C2:C0E8) $0D76=03*(C2:EB15) $0067=04` |
| G10 | **sub-state 3 (cells $0C..$0E, entered by A on cell 7): L/R moves the cursor and writes `$0D8A`** | C2/BF9F `JSR C2/E984 / BFA2 LDA $0D1D / AND #C0 / BFA7 BNE ->BFC5 / BFA9 LDA $54 / CMP $81 / BFAD BEQ ->BFC4 / SEC SBC #0C / STA $0D8A / … / **BFC1 JSR C2/EAC2**`; entered by C2/C080 `LDA $0D1D / BPL ->C094 / LDA $0D8A / CLC ADC #0C / STA $54 / INC $0F0D / LDA #03 / STA $68` | 18 | 00 | cell changed | y | [dis+orc] `f159 $0054=0E*(C2:EA4A) $0D8A=02*(C2:BFB5) $0D76=03*(C2:EB15)`, f172, f185 |
| G11 | **sub-state 3 exit (A/B)** | C2/BFA7 `BNE ->BFC5` → C2/BFC5 … → **C2/BFD1 `JMP C2/EAC2`** (JMP-form, `4c c2 ea` at 0x2BFD1) | 18 | 00 | A or B | y | [dis] |
| G12 | Config **B → ribbon** | C2/BF50 `BVS ->BF5F` → C2/BF6A `JMP C2/9A98` → C2/9AAA | 18 | 00 | shared back-path | y | [dis] |

### 1.7 FORM (screen 5) — `src/field.c:7911-7933`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| P1 | entry gate — see **F1/F2** above | C2/C627 | 18 | 00 / **01** | `($0F0B+$0F0C) >= 2` | y | [dis+orc] |
| P2 | picker Up/Down, A first pick, A second pick (swap), A on the same member, B cancel/exit | sites in screen 5: C2/C642 C670 C674 C6CC C6DE (`JSR C2/EAC2`) | 18 | 00 | **⚠ the source batch's Form rows are TRUNCATED in the material handed to me.** The site census is certain; the per-site branch conditions are **NOT restated here** — see §4 | y | [dis] census |
| P3 | Form **B → ribbon** — the ONE sub-screen that does NOT jump straight to C2/9A98 | C2/C665 `BVS ->C6B3` → **C2/C6B3 `JSR C2/C6E7` FIRST**, then C2/C6B6 `JMP C2/9A98` → C2/9AAA | 18 | 00 | the other five screens go straight in | y | [dis] |

`$0F0B`/`$0F0C` are **screen-shared scratch** written as CONFIG toggle mirrors (C2/C038, C2/C06E)
and rebuilt by **C2/CD2B when the ribbon cursor lands on the Form tab**, not by C2/C627 itself —
measured `f85 7E0F0B=01*(C2:CD54) 7E0F0C=00*(C2:CD43) 7E0411=04*(C2:9A48)`.

### 1.8 SAVE (screen 6) — `src/field.c:7934-7942`

| # | action | ROM site | cmd | id | gate | thr | confirmed-by |
|---|---|---|---|---|---|---|---|
| S3 | slot cursor **Up/Down** over the 3 slots | C2/CEFD → C2/CF07 `JMP C2/CFA2`; `CFA2 PHP / SEP #20 / CFA5 LDA $54 / STA $79 / CFA9 CMP $7F / STA $7F / CFAD BEQ ->CFB2 / **CFAF JSR C2/EAC2**` | 18 | 00 | `$54` vs its previous-frame copy `$7F` | y | [dis+orc] `f185 $0054=01*(C2:EA4A) $0079=01*(C2:CFA9) $0D76=03*(C2:EB15)`; f207 for the second |
| S4 | **A on an EMPTY slot (`$0D49,X == 0`) = THE SAVE-WRITE → THE SAVE JINGLE** | C2/CF0A `LDX $79 / LDA $0D49,X / CF0F BEQ ->CF1E` → C2/CF1E `JSR C2/CF37`; **C2/CF37 writes the mailbox INLINE**: raw `A9 19 8D 00 1E / A9 8A 8D 01 1E / A9 80 8D 02 1E / 22 04 00 C7 / A6 79 / A9 FF / 9D 49 0D / 22 FB F9 FF (the SRAM write) / 20 1E 82 / 20 9F D1 / 4C 1E 82` | **19** | **8A** | `$0D49,X == 0`. `$0D49[0..2]` is per-slot occupancy: C2/D16A `STZ $0D49,X` / D16D `JSL $FF:F9C4` (probe SRAM) / D173 `BEQ ->D17F` / **D17F `INC $0D49,X`** ⇒ 1 = valid save, 0 = "No data" | **n — and it never touches `$0D76` at all** | [dis+orc] `f185 7E1E00=008A800D*(C7:0196) $0079=00 **$0D76 stays 00** $0D49=FF0000*(C2:CF51)` |
| S5 | **A on an OCCUPIED slot → the OVERWRITE-CONFIRM prompt (sub-state 2)** | C2/CF0F BEQ **not** taken → **C2/CF11 `JSR C2/EAC2`** / CF14 `LDA $54 / STA $0F00` / CF19 `INC $68` / CF1B `JMP C2/D4BB` (which seeds `$54 = 3` = YES, `2D4BE LDA #03 / 2D4C0 STA $54`) | 18 | 00 | `$0D49,X != 0` | y | [dis+orc] `f287 7E1E00=0000800D*(C7:0196) $0054=03*(C2:D4C2) $0D76=03*(C2:EB15) $0D49=FF0000` |
| S6 | prompt: cursor **Up/Down** between YES(3) and NO(4) | C2/CF6B `LDA $54 / CMP $81 / CF6F BEQ ->CF74 / **CF71 JSR C2/EAC2**` | 18 | 00 | `$54 != $81` | y | [dis+orc] `f349 $0054=04*(C2:EA4A) $0D76=03*(C2:EB15)` |
| S7 | prompt: **A on YES → confirm the overwrite** | C2/CF67 `BMI ->CF75` → `CF75 LDA $54 / CMP #03 / CF79 BNE ->CF88` (not taken) → **C2/CF7B `JSR C2/CF37`** | **19** | **8A** | `$54 == 3` | **n** | [dis+orc] `f349 7E1E00=008A800D* $0054=03 **$0D76=00** $0D49=FF0000(C2:CF51)`, then `f352 $0054=00*(C2:D4E8)` — **the jingle posts and there is NO blip** |
| S8 | prompt: **B, or A on NO** → cancel back to the slot list | C2/CF69 `BVS ->CF88`, or C2/CF79 `BNE ->CF88` → **C2/CF88 `JSR C2/EAC2`** / CF8B `$68 := 1` / CF8F `JMP C2/D4D5` | 18 | 00 | either arm | y | [dis+orc] `f371 7E1E00=0000800D(C7:0196) $0054=00*(C2:D4E8) $0D76=03*(C2:EB15)` |
| S9 | **B on the slot list → ribbon** | C2/CF05 `BVS ->CF5E` → C2/CF5E `JMP C2/9A98` (raw `4c 98 9a`) → C2/9AAA | 18 | 00 | shared back-path | y | [dis+orc] `f205 $0067=00*(C2:9A9E) $0411=05 $0D76=03*(C2:EB15)` |
| S10 | screen-6 **sub-state 3** | C2/CF92 | — | — | **UNREACHABLE ⇒ silent.** Full census of every `$68` writer in bank C2 (STA/STX/STY/INC/DEC/STZ): the only immediate-3 stores are C2/C092, D583, D6B5, E25D, E62A — all in **other** screens; inside screens 06 and 0B the only `$68` writes are CEF0(=1) CEF8(INC→1) CF19(INC→2) CF80(=1) CF8D(=1) D501(INC→1). `$B6,X` (C2/820F `LDA $B6,X / STA $68`) cannot fabricate a 3 — it is only ever a MIRROR of `$68` (C2/81FB `STZ $B6,X` beside `STZ $68`) | n | [dis] |

---

## 2. THE OPEN PRELUDE

One caller: **C2/8068 `JSR C2/8096`**, reached on **every** mode-3 open (menu included). Raw bytes
`d[0x28096:0x280F6]`, re-read in this pass:

| order | ROM | bytes | cmd | $1E01 | $1E02 | ships at |
|---|---|---|---|---|---|---|
| 1 | C2/80AA-80B2 | `A9 70 8D 00 1E 9C 01 1E 22 04 00 C7` | **$70** | **00** (STZ) | *unwritten* | C2/80B2 |
| 2 | C2/80B6-80C5 | `A9 18 8D 00 1E A9 FF 8D 01 1E A9 80 8D 02 1E 22 04 00 C7` | **$18** | **FF** | **80** | C2/80C5 |
| 3 | C2/80C9-80CE | `A9 19 8D 00 1E 22 04 00 C7` | **$19** | **FF (carried)** | **80 (carried)** | C2/80CE |
| 4 | C2/80D2-80E1 | `A9 82 8D 00 1E A9 01 8D 01 1E A9 FF 8D 02 1E 22 04 00 C7` | **$82** | **01** | **FF** | C2/80E1 |
| 5 | C2/80E5-80EA | `A9 83 8D 00 1E 22 04 00 C7` | **$83** | **01 (carried)** | **FF (carried)** | C2/80EA |

Measured, one drive, `--lifetrace --wwatch "7E1E00:4"`:
`W 7E1E00 <- 70 @C2/80AF` · `W 7E1E01 <- 00 @C2/80B2` · `W 001E00 <- 00 @C7/0196` ·
`W 7E1E00 <- 18 @C2/80BB` · `W 7E1E01 <- FF @C2/80C0` · `W 7E1E02 <- 80 @C2/80C5` ·
`W 7E1E00 <- 19 @C2/80CE` · `W 7E1E00 <- 82 @C2/80D7` · `W 7E1E01 <- 01 @C2/80DC` ·
`W 7E1E02 <- FF @C2/80E1` · `W 7E1E00 <- 83 @C2/80EA` — all in ONE frame (f24 in that run).

**None of the five goes through C2/EB03**, so none can be throttled and none arms `$0D76`.

### What is KNOWN about what they do

* **$70 = music pause/resume**, `$1E01 = 0` = **resume**, and on a normal open it is a **NO-OP**:
  `C7/08ED CMP $11 / C7/08EF BEQ ->7091F` exits without touching `$2140` when `$1E11` is already 0.
  Measured: `$1E10/$1E11` stay `00/00` across the whole open (`7E1E00:20` diff shows offsets
  0x10/0x11 unchanged f0→f24).
* **$18 id $FF = STOP ALL SFX** — already derived in this repo: `disasm/notes/audio_commands.md`
  line 343, "C1/40D3 — `$1E01 = $FF`, cmd $18 = **stop all SFX**".
* **$82 / $83 = raw Akao driver commands** with operands `(01, FF)`. Measured **not** a music duck:
  `--aram` reports `dsp mvol=64/64 evol=15/15 flg=18` **both** outside the menu
  (`voicesWithGain=5`) and inside it (`voicesWithGain=4`).

### What is NOT known — and the porting risk

* **cmd $19's meaning is not derived.** It is used three ways in the menu's reach: prelude post 3
  (id $FF), the **save jingle** (id $8A, C2/CF37), and C2/EAF9 (id $06/$07). Post 3 re-uses the
  *stop-all* id `$FF` verbatim.
* **$82 and $83 are not decoded.** We know they change no DSP master/echo volume and no voice
  count in the measured window. Anything beyond that is NOT DERIVED.
* **NO CLOSE-SIDE COUNTERPART WAS FOUND.** The menu close (row R7) is *totally* silent and posts
  nothing at all; there is no `$70`-resume, no `$83`-undo. **Port risk: if $82/$83 (or the $70 with a
  non-zero `$1E11`) turn out to pause or mute anything, our engine would open the menu and never
  restore it, because the ROM's restore — if it exists — is not on the close path we measured.**
  Recommendation: port posts 1-3 (whose semantics are derived) and **hold 4/5 behind a derivation**,
  or port all five and pin an ARAM invariant check (`--aram` mvol/evol identical before the open and
  after the close).

---

## 3. ROM sounds our engine has NO counterpart for

Actions the ROM sounds that our `menuPhase` machine cannot even reach (so the sound is missing
*with* the behaviour, not instead of it):

1. **The whole open prelude** (5 posts) — `src/field.c:7712-7743` fades and posts nothing.
2. **Ribbon auto-repeat** (R4) — our nav is EDGE-only (`src/field.c:7772`), so the +32/then-every-5
   repeat train, and its blips, do not exist.
3. **Char-select confirm's SECOND blip** (E5) — our `A → detail` at `src/field.c:7830-7833` is
   instantaneous; the ROM's coroutine yield produces a second post 25 frames later.
4. **Equip detail L/R character switch** (E8/E9) — not implemented at all.
5. **The equip gear list** in full: E10 (the id-$01 refusal), E11, E12, E14, E15 and **E16, the only
   id-$B5 sound in the menu** — our Equip detail never opens a list and never commits gear.
6. **The Item USE target-select's buzz** (I14) and its cursor/exit blips (I13/I15) — we have a
   target-select (`src/field.c:7858-7869`) but no `$0DBC` validity test, hence no refusal at all.
7. **Config's raw `cmd $F3` stereo toggle** (G3) — our Config defers every toggle
   (`src/field.c:7903-7910`), so this driver command is never posted.
8. **Config's sliders and sub-modes** (G4, G5, G6, G9, G10, G11) — no slider sub-mode exists in C.
9. **The Save write and its jingle** (S4, S7, cmd $19 id $8A) and **the overwrite-confirm prompt**
   (S5, S6, S8) — `src/field.c:7941` is the marked deferral, so A on a slot is inert.
10. **The Tech screen's five `JMP C2/EACC` refusals** (E18) — our Tech detail is read-only.
11. **`C2/9047` → `C2/EAF9`: cmd $19 with id $06 (from C2/9027) and id $07 (from C2/903F)** —
    unthrottled by construction (`STZ $0D76` before the test). `C2/9016 SEP #30 / LSR $0F5D /
    BCC ->902C / STZ $0F60 / LDA #03 STA $0F61 / STZ $16 / 9027 LDA #06 / 9029 JSR C2/9047`.
    Both callers verified by byte scan (`20 47 90` at 0x29029 and 0x29041; no `4C 47 90`).
    **Reachability NOT reproduced** — see §4.

---

## 4. WHAT IS NOT DERIVED

* **`$0F0B` / `$0F0C` semantics.** The Form gate's predicate is derived byte-exactly
  (`LDA $0F0B / CLC / ADC $0F0C / CMP #02 / BCS`), but the gloss "party < 2" is **not**: what the
  two bytes count was never established.
* **`$0110`** (the third open gate) is unlabeled in the ROM; it reads 00 in every test state.
* **cmd `$19`, `$82`, `$83`, `$F3`, `$11`, `$14`, `$81` semantics** in the Akao driver. Only cmd
  `$18` (play/stop SFX) and `$70` (pause/resume) are derived, and both from prior dossiers.
* **The equip gear list (screen 1 sub-state 3) is UNDRIVABLE in this repo.** I confirmed the excuse
  rather than trusting it: `$7E:2400 + 242` was read out of **all 104** `.state` files (92 parsed;
  12 are raw snes9x states) — **zero** contain any item id in `0x01..0xBB`, so C2/8953 always leaves
  `$0FC6 = 0` and `C2/9C8B BNE ->9C90` never takes. `ct.exe` has no WRAM-poke mode. **Rows E11, E12,
  E14, E15, E16 stand on disassembly alone.**
* **The tech-fire path (`$0F5D`) was never reached.** Three attempts on
  `saves/battle_re/party3_field.state`, including a 2200-frame mash confined to the tech screen:
  `--pcwatch "C2/9047,C2/EAF9"` reports **zero** hits and `--monitor "7E0F5D:1" --diff` never leaves
  00 across the run. The two setters are C2/8FE6 (`LDA #$0001 / STA $0F5D`) and C2/9008
  (`ORA #$0002`), neither fired. Per the repo's rule, this is **"not reproduced under X"**, never
  "absent".
* **Item USE and the `$0DBC` buzz (I14) were not driven** — no repo state reaches an invalid target.
* **Rows E17/E18 (Tech L/R and the five Tech refusals) and P2 (Form picker) are UNDER-SPECIFIED
  HERE:** the source batches covering them were **truncated in the material handed to me**. Their
  *site censuses* are certain (byte-scan reproduced), their *branch conditions* are not restated.
  **Anyone implementing those rows must re-derive the gates first.**
* **Frame offsets are not modelled.** The ROM's "blip one frame after `$0067` changes" (R6→E1/I1/…)
  and the 25-frame / 7-8-frame second blips (E5, E12) come from the mode-3 coroutine (`C2/821E`),
  which our engine does not have.
* **Three bank-C2 mailbox sites are OUTSIDE the menu's reach and were not derived:**
  `C2/CBD0` (cmd $14 id $3E) / `C2/CBF9` (cmd $81) / `C2/CC3B` (cmd $11 with `$0F0E`) live in
  **mode-3 screen $07** (screen table `[07] = C2/C7A6`; its sub-table `d[0x2C7A6:0x2C7AC]` =
  `AC C7 E0 C7 B9 C6` — note it **shares C2/C6B9 with the Form screen**), reached by request kind 5
  (`d[0x2808F+5] = 07`), not by the ribbon. Likewise `C2/6BDC` (an **unthrottled cmd $18 id $01**
  buzz, `C2/6BD8 LDA #18 … LDA #01 … LDA #80 … JSL $C7:0004 / CLC / RTS`, no JSR/JMP caller in bank
  C2 — a branch-target continuation), and `C2/E696` (cmd $F3 with `$2990 & 08`) in screen $0E.

---

## 5. THE PORT

### 5.0 What already exists (do NOT re-derive or duplicate)

* **`fieldMenuSfx(uint8_t* m, uint8_t id)` — `src/field.c:1607`** is already C2/EB03, byte-exact:
  `m[0x1E00]=0x18` / `m[0x1E01]=id` / `if (m[0x0D76]) return 0;` / `m[0x0D76]=3` / `m[0x1E02]=0x80` /
  `fieldSoundFlush(m)`. It returns 1 = shipped, 0 = throttled. **Reuse it. Do not write a second
  stager.**
* **`fieldMenuSfxTick(uint8_t* m)` — `src/field.c:1622`** is C2/845E-8463.
* **The env probes already exist**: `CT_MENUSFX` (per-post line) and `CT_MENUSFX_COUNT`
  (one summary at the end), plus the liveness line **`menusfx-menu: phase=… screen=… opt=… sub=…
  itemrow=… label=… pick=…`** already printed at **`src/field.c:8347`**.
* **The drive token for X already exists** — the task brief's premise is wrong. `'m'` is the X key:
  `src/field.c:7206` (`int wsB = 0, wsStart = 0, wsMenu = 0;  // wsMenu = the X key`),
  `:7283`, `:7312` (`else if (c == 'm' && frameNo == at) wsMenu = 1;   // X (open the menu)`),
  consumed at `:7678` (`if (wsMenu) xHeld = 1;`). **No new hook is needed.** In the `--play`
  grammar the token is `m:1` and, like `a`/`b`/`s`, it fires on the frame the step begins.

### 5.1 A new tally namespace (mirror of `nameSfx`)

Add beside `g_nameSfx*` (`src/field.c:1638-1653`) — **separate counters**, because
`menusfx-count:` is already pinned by two ATB checks whose wording is row-specific:

```c
static int g_mnSfxPosts = 0, g_mnSfxEvents = 0, g_mnSfxSilent = 0, g_mnSfxSeen = 0;
static void menuSfx(uint8_t* m, uint8_t id, const char* what);   /* -> fieldMenuSfx */
static void menuSfxSilent(const char* what);                     /* a ROM silence, counted */
static void menuSfxRaw(uint8_t* m, uint8_t cmd, uint8_t id, uint8_t pan, int panWritten,
                       const char* what);                        /* prelude + $19 + $F3, no throttle */
```

and ONE summary line at the end of the run (never a running tally — `expect` is a substring match
and a per-post count lets a mutation survive green):

```
menusfx-menu-count: N post(s) from M event(s), K silent
```

### 5.2 Where each call goes

| src/field.c anchor | action | call |
|---|---|---|
| `:7734` (`menuPhase = 2` — the moment the mode-3 shell comes up) | **the OPEN PRELUDE** | five `menuSfxRaw`: `(0x70,0x00,-,no pan)`, `(0x18,0xFF,0x80)`, `(0x19,0xFF,0x80)`, `(0x82,0x01,0xFF)`, `(0x83,0x01,0xFF)` — in that order, **all in one frame, none throttled**. See §2's port risk before enabling 4/5. |
| inside the `menuPhase == 2` block, once per frame (`:7745`) | the throttle drain | **`fieldMenuSfxTick(s7);`** ⚠ **the existing call at `src/field.c:8700` is UNREACHABLE from the menu** — the menu block `continue`s at `:8353` before it. Its guard is `(atbGate && atbSelPhase) \|\| (nameId >= 0 && nmPhase)`. **NOT** in `menuPhase == 1`: the C0/1943 fade runs *before* mode-3 exists. |
| `:7712-7743` (phase 1 fade) | A2 | `menuSfxSilent("entry-fade")` once, so the silence is graded and not a dead arm. |
| `:7773` (`menuOpt = CtMenuRibbonStep(...)`) | R1/R2 | `menuSfx(s7, 0x00, "ribbon-tab")` **only when the tab actually changed** (mirror C2/9A2C: compare before/after). |
| `:7771` ribbon, Up/Down | R3 | nothing — and our engine already polls no Up/Down here. |
| `:7778` (ribbon A) | R6 | **post NOTHING here.** |
| `:7779` (`menuScreen = menuOpt + 1;`) and each accepted branch | E1/I1/T1/C1/F1/S1 | `menuSfx(s7, 0x00, "screen-open")` **at the point the screen is committed** (after the Save/Form gates below). Boundary to state in the comment: the ROM posts it one frame later, from the destination's sub-state 0. |
| `:7795-7796` (`if (!CtMenuSaveCanOpen(...)) menuScreen = 0;`) | **S2** | `menuSfx(s7, 0x01, "save-refuse")` — **the refusal buzzes** (C2/CEF2 → EACC). |
| `:7810` (the Form refusal `menuScreen = 0;`) | **F2** | `menuSfx(s7, 0x01, "form-refuse")` (C2/C63A → EACC). |
| `:7819` (`closeReq = 1`) | R7 | `menuSfxSilent("menu-close")`. |
| `:7823-7829` (char-select Up/Down) | E2/E3 | `menuSfx(s7, 0x00, "charsel")` **only when the slot actually changed**; else `menuSfxSilent("charsel-refused")`. |
| `:7830-7833` (A → detail) | E4 | `menuSfx(s7, 0x00, "charsel-confirm")`. **Do NOT synthesise E5** (see §5.3). |
| `:7835` (`bHeld` → `menuScreen = 0`) — and the identical lines `:7877`, `:7910`, `:7942`, `:7947`, plus the Form exit at `:7930-7932` | E6/I11/G12/S9/P3 | `menuSfx(s7, 0x00, "back-to-ribbon")` — **one shared call**, because the ROM has one shared site (C2/9AAA). |
| `:7838-7839` (equip gear-slot Up/Down) | E7 | `menuSfx(s7, 0x00, "equip-slot")` when `menuEquipSlot` changed. |
| `:7843` (`menuStatSub = 0`) | E13 | `menuSfx(s7, 0x00, "equip-back")`. |
| `:7879-7887` (item list Up/Down + scroll) | I2/I3/I4 | `menuSfx(s7, 0x00, "item-cursor")` when **row OR scroll** moved; `menuSfxSilent("item-edge")` when neither. |
| `:7888-7897` (item A) | I5/I6/I8 | `menuSfx(s7, 0x00, "item-a")` **unconditionally on every A**, before the pick/swap/use decision — that is exactly where C2/AA99 and C2/AAD5 sit. ⚠ our `:7890` refuses an empty slot (`if (s7[0x2400+absSlot])`); **the ROM picks it and blips.** Fixing that is a behaviour change, not a sound change — pin it separately. |
| `:7898-7901` (item B) | I7/I9 | picked → `menuSfxSilent("item-pick-cancel")`; list → `menuSfx(s7, 0x00, "item-b-to-labels")`. |
| `:7871` (label L/R) | I10 | `menuSfx(s7, 0x00, "item-label")`. |
| `:7872-7876` (A on Organize) | I12 | `menuSfx(...)` for the A itself (C2/AA5E path), then `menuSfxSilent("organize-sort")` for the sort. |
| `:7860-7869` (target-select) | I13/I15 | `menuSfx(s7, 0x00, "target-cursor")` on a move, `menuSfx(s7, 0x00, "target-b")` on B. **I14's buzz needs the `$0DBC` test we do not have — leave it out and say so.** |
| `:7908-7909` (config Up/Down) | G1 | `menuSfx(s7, 0x00, "config-cursor")` when the cell changed. |
| config A (we have none — `src/field.c:7903-7910` has no `aHeld` arm) | G2 | **add nothing until the toggles exist.** G2 is unconditional in the ROM, so wiring it to our deferred no-op A would be sound without a mechanism. |
| `:7939-7940` (save slot Up/Down) | S3 | `menuSfx(s7, 0x00, "save-cursor")` when `menuSaveCur` changed. |
| `:7941` (the deferred A) | S4/S5 | **the ROM's two arms are already distinguishable with data we HAVE** — `CtMenuSaveSlot()` (src/menu.c, used by `CtMenuRenderSave` for the "No data" marker) is the `$0D49,X` occupancy. Occupied → `menuSfx(s7, 0x00, "save-overwrite-prompt")`. Empty → the save WRITE is deferred, so post **nothing** and `menuSfxSilent("save-write-deferred")`; **do not post the $8A jingle for a save that does not happen.** |
| `:7917-7923` / `:7924-7929` / `:7930-7932` (Form) | P2 | **hold.** The Form rows are under-specified (§4). |

### 5.3 What must NOT be invented

1. **No sound on the ribbon A press itself** (R6) and **no sound on the menu close** (R7).
2. **Do not synthesise the second blips** (E5 at +25f, E12 at +7-8f). They are products of the mode-3
   coroutine `C2/821E`, which we do not model. Posting them "close enough" is eyeballing a cadence.
3. **Do not gate on our own convenience flags.** Every gate above is a named RAM byte
   (`$0411`, `$0054`/`$81`, `$83`, `$0FC5`, `$0FC6`, `$0415`/`$0416`, `$0D49,X`, `$7F`, `$0D34`,
   `$0F0B+$0F0C`, `$0D1D`). Where our engine lacks the byte (`$0DBC`, `$0FC6`, `$0F5D`), **omit the
   sound and record the omission** — do not approximate it with a C-side heuristic.
4. **Do not add a per-screen stager or a per-screen throttle.** There is exactly ONE stager and ONE
   `$0D76`; `fieldMenuSfx` already is it.
5. **Do not post cmd $19 id $8A** anywhere until the save actually writes (§5.2, `:7941`).
6. **Do not "fix" the item-list empty-slot refusal by adding a blip** — the ROM's blip is a
   consequence of a different *behaviour* (it picks the empty slot). Change the behaviour or leave
   both alone.
7. **Do not port the id-$B5 equip commit** (E16): the path is unreachable and untested (§4).

### 5.4 Headless regress checks (model: `field-naming-*`)

Common `env`: `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `CT_AUDIO_FORCE=1`,
`CT_MENUSFX=1`, `CT_MENUSFX_COUNT=1`. Common `argv` shape:
`build/Belthasar.exe --assets ct_assets.dat --scene <N> --selftest build/_mnsfx.raw --frames <F>
--play "<script>"`. Drive grammar: `u/d/l/r` on **EDGE**, `a` = A, `b` = B, `s` = START,
**`m` = X = open the menu**, `w`/`wait` = idle. **No repeat operator — expand literally.**

| check | script | expect (substring) | mutation it must kill |
|---|---|---|---|
| `field-menu-open-prelude` | `wait:20; m:1; wait:40` | `menusfx-menu: prelude 5 -> 70/00, 18/FF, 19/FF, 82/01, 83/01`<br>`menusfx-menu-count: 5 post(s) from 6 event(s), 1 silent` | delete post 2 → `4 post(s)` |
| `field-menu-ribbon-nav-sfx` | `wait:20; m:1; wait:30; r:1; w:6; r:1; w:6; l:1; wait:20` | `menusfx-menu: ribbon-tab -> cmd=18 id=00 pan=80 posted` ×3 and `… 8 post(s) from 9 event(s), 1 silent` | remove the "tab actually changed" compare → an extra post on a no-move |
| `field-menu-ribbon-updown-silent` | `wait:20; m:1; wait:30; u:1; w:6; d:1; wait:20` | `menusfx-menu-count: 5 post(s) from 5 event(s), 2 silent` (**both numbers**, so a dead arm cannot rot green) | add a blip to Up/Down → posts 5→7 |
| `field-menu-open-a-silent-screen-blips` | `wait:20; m:1; wait:30; a:1; wait:20` + `menusfx-menu: … screen=1` | `menusfx-menu: screen-open -> cmd=18 id=00 pan=80 posted` and **no** `ribbon-a` line | post on the A press instead → an extra event |
| `field-menu-close-silent` | `wait:20; m:1; wait:30; b:1; wait:30` | `menusfx-menu: menu-close -> SILENT (no post)` + the count line | make the close blip → posts +1 |
| `field-menu-save-refuse-buzz` | field scene: `wait:20; m:1; wait:30; l:1; w:6; a:1; wait:20` (Left from tab 0 wraps to **5 = Save**) | `menusfx-menu: save-refuse -> cmd=18 id=01 pan=80 posted` | change id 01→00 → the string fails |
| `field-menu-form-refuse-buzz` | `wait:20; m:1; wait:30; r:1; w:6; r:1; w:6; r:1; w:6; r:1; w:6; a:1; wait:20` (tab 4 = Form) | `menusfx-menu: form-refuse -> cmd=18 id=01 pan=80 posted` | drop the EACC → 0 posts |
| `field-menu-throttle-drains` | `wait:20; m:1; wait:30; r:1; w:1; r:1; w:1; r:1; w:1; r:1; wait:20` (4 moves, 1 frame apart) | `menusfx-menu-count: … post(s) from 4 event(s), …` with **posts < events** — the 3-frame `$0D76` window swallows the middles | remove `fieldMenuSfxTick` from the menu block → **posts drop to 1** (one blip then silence forever); remove the throttle test → posts == events |
| `field-menu-item-cursor-sfx` | `wait:20; m:1; wait:30; r:1; w:6; a:1; wait:20; d:1; w:6; d:1; wait:20` | 2 × `item-cursor` posted, plus the `screen-open` | remove the row/scroll compare |
| `field-menu-item-edge-silent` | `…; u:1; w:6; u:1; wait:20` at row 0 / scroll 0 | `menusfx-menu-count: … , 2 silent` (**and** the events number, for liveness) | blip at the edge → silent 2→0 |
| `field-menu-save-cursor-sfx` | needs an **overworld** state (`saves/ow_w0.state` is where the Save gate passes) | `save-cursor` ×2 + `screen-open` | — |

**Anchor every check on the EVENT (posts/events/silences), never on a frame number** — the repo has
already had a frame-pinned SFX check rot green with its mutation alive.