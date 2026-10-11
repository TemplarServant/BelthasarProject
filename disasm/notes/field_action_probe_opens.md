# The ACTION probe — closing the four OPEN items (oracle-measured)

Companion to `field_action_probe.md` (commit `57c7822`), whose section 6 left four items open.
Every claim below carries the command that produced it. Claims I could **not** measure are marked
**NOT DERIVED** or **disasm-only, unconfirmed** in place.

Working states used (all built here, in the scratchpad, from committed saves — none of them are in
`saves/`):

```
# scene 2 (Crono's room), driven from saves/wake.state until $011F flips to 1 at f681
PEEK_SAVE=<scratch>/s2_ctl.state build/ct.exe --peek saves/wake.state <scratch>/s2_ctl.raw \
  --btnscript "<48x '0:8,100:6'>,0:150"
# then walked UP:   s2_atwall.state = "10:60,0:6"   (Y clamped at the wall)
#                   s2_y35/36/37    = "10:35" / "10:36" / "10:37"
# scene 439, menu closed:  saves/menu_open_fair.state + "0:6,1:6,0:120"  -> s439.state
```

⚠ **Every scene-2 savestate in `saves/` (`wake`, `bed3460`, `slide`, `slide168`) has `$011F == 0`,
even after `--settle 240`** — i.e. the A-button path is *closed* in all of them. Reading "the probe
never fires in scene 2" off those states is a FALSE NEGATIVE of exactly the kind
`verify-negatives-on-proper-state` warns about. The state has to be driven to f681 first:

```
build/ct.exe --monitor "7E011F+1,7E0100+2,7E01EB+2" --from saves/wake.state --btnscript "<180x '0:8,100:6'>" --diff
  f0      7E011F=00*  7E0100=02*  7E01EB=80*
  f681    7E011F=01*  7E0100=02  7E01EB=80
```

---

## 0. Corrections to `field_action_probe.md` before anything else

### 0a. The `1AB4` guard in the dossier is a **desynced disassembly**

The dossier prints `1AB4 LDX $34 / BEQ ->1AC1 ... STX $34 / RTS`. `tools/dis.py` defaults to 8-bit
X and desyncs on the `CPX #FFFF` immediate. `X is 16-bit here`; with `x=0` it parses cleanly and
lands exactly on `1AC1`:

```
python tools/dis.py 1AB4 0x14 1 0
  1AB4: LDX  $34
  1AB6: BEQ  ->1AC1
  1AB8: CPX  #FFFF
  1ABB: BNE  ->1AC1
  1ABD: INX
  1ABE: STX  $34
  1AC0: RTS
```
raw bytes (`A6 34 F0 09 E0 FF FF D0 04 E8 86 34 60`). **Only `$0134 == $FFFF` blocks.** Zero passes
(`BEQ`), and any other non-zero value *also* passes (`BNE ->1AC1`).

### 0b. `DP = $0100` is now MEASURED, not assumed

`C0/8820` stores the "no target" sentinel to the **absolute** address `$01EB` before it swaps DP:

```
python tools/dis.py 881E 0x18 1 0
  881E: LDA  #80
  8820: STA  $01EB
  8823: PHD
  8826: LDA  #1D00 / 8829: TCD
```
and the oracle's write hook attributes the `$7E01EB` byte to that store every frame:
```
build/ct.exe --monitor "...,7E01EB:1,..." --from <scratch>/s2_ctl.state --btnscript "10:60,0:6,100:4,0:60" --diff --writer
  f68     7E01EB=80*(C0:8823)  ...
```
So `$EB` = `$01EB`, and by the same run `$34`=`$0134`, `$1F`=`$011F`, `$97`=`$0197`, `$DB`=`$01DB`,
`$C3/$C5`=`$01C3/$01C5`. **`$01EB` is reset to `$80` at the TOP of every frame by the player
controller `C0/881E`** — that is why `1AC6 LDA $EB / BPL` normally falls through to the probe.

### 0c. Section 3's *attribution* of the borrow is **REFUTED** (see §3). The borrow is real; its
source is `CPX $97` at `1D36`, not `CMP #07` at `1D32`.

---

## 1. The `$0134` and `$011F` guards at `C0/1AB4` / `C0/1AC1`

### `$0134` = a one-shot "**swallow the next A press**" latch, armed by the MENU

Writers, measured:

```
build/ct.exe --monitor "7E011F+1,7E0134+2,7E01EB+2" --from saves/wake.state \
  --btnscript "0:20,100:6,0:20,100:6,...,0:120" --diff --writer
  f0      7E011F=00*  7E0134=FF*  7E01EB=80*(C0:8823)
  f21     7E011F=00  7E0134=00*(C0:1AC0)  7E01EB=80(C0:8823)
  f245    7E011F=00  7E0134=FF*(C0:21D8)  7E01EB=80(C0:8823)
  f303    7E011F=00  7E0134=00*(C0:1AC0)  7E01EB=80(C0:8823)
```

* `C0:1AC0` is the post-PC of `1ABE STX $34` — the guard consuming itself (`INX` on `$FFFF` → `0`).
* `C0:21D8` is the post-PC of `21D6 STX $34`, inside the **menu-exit dispatcher**:

```
python tools/dis.py 2160 0x64 1 0
  216A: JSR  C0/20B2
  216D: LDA  #01 / 216F: STA $0213      ; open the menu
  2172: JSL  C20009                     ; THE MENU (bank C2)
  2176: LDA  $0215                      ; the menu's exit code
  ...  2179/2187/218D BEQ ->21D3   217F/2185 BEQ ->21D8   2191/2195 ->21CB   2199/219D ->21C2
  21BB: LDX  $4216 / 21BE: STX $34      ; (a computed value, see NOT DERIVED below)
  21C2: LDX  #FFFF / 21C5: STX $34
  21D3: LDX  #FFFF / 21D6: STX $34
```

**End-to-end proof that the latch swallows the press** — `--hookpc` on the probe's own entry:

```
build/ct.exe --hookpc 1CFC --from saves/menu_open.state \
  --btnscript "0:10,1:6,0:60,100:6,0:40,100:6,0:20" --frames 150 --hookwatch 7E0134
  f75   visits=0   end=C2/5E3B [7E0134]=FF
  f76   visits=0   end=C2/5DD8 [7E0134]=00      <-- FIRST A press: latch consumed, probe NOT reached
  ...
  f122  visits=1   end=C0/1DF6 [7E0134]=00      <-- SECOND A press: probe runs
```
(`saves/menu_open.state` already carries `$0134 = FFFF` at f0 — `build/ct.exe --read 7E0134+2 --from
saves/menu_open.state` → `FFFF` — because the menu is open in that capture.)

### `$011F` = the script-set control/"explore" enable, VM opcode writer `C0/3C05`

```
build/ct.exe --monitor "7E011F:1,7E0134:2" --from saves/wake.state --btnscript "<50x '0:8,100:6'>" --diff --writer
  f681    7E011F=01*(C0:3C0D)  7E0134=0000

python tools/dis.py 3C05 0x14 1 1
  3C05: INY / 3C06: TYX
  3C07: LDA  $7F2001,X
  3C0B: STA  $1F            ; post-PC C0/3C0D = the reported writer
  3C0D: INY / 3C0E: TYX / 3C0F: SEC / 3C10: RTS
```
i.e. an event-VM opcode that stores its immediate operand into `$011F` (this repo already knows it
as op `0xE3`, memory `ct-field-control-lock`). **`$011F == 0` refuses the A press outright**, measured
by counting probe entries across 760 frames of A-mashing:

```
build/ct.exe --hookpc 1CFC --from saves/wake.state --btnscript "<55x '0:8,100:6'>" --frames 760 --hookwatch 7E011F | grep -cE "visits=[1-9]"
  5
build/ct.exe --hookpc 1CFC ... | grep -E "visits=[1-9]" | head
  f694  visits=1   end=C0/59F6 [7E011F]=01 ...
  f708  visits=1   end=C0/304A [7E011F]=01 ...
  f722 / f736 / f750  ... [7E011F]=01
```
~55 A presses, **zero** probe entries while `$011F == 0`; the first entry is 13 frames after `$011F`
flips to 1. Guard order matters: `$0134` is tested **before** `$011F`.

**NOT DERIVED**: the caller chain that reaches `C0/2166`-`21E0` (the only static xref into that block
is `JSR 2169 from 01200D`), the meaning of `$0215`'s exit codes, and what consumes a **non-`$FFFF`**
`$0134` (e.g. the `21BB LDX $4216 / STX $34` product). A non-`$FFFF` value does **not** block the
probe — `1ABB BNE ->1AC1` passes it through.

---

## 2. Who carries `$1100,X == 7` — the arm at `C0/1D48`

`$1100,X` is a per-object byte at stride 2 (`$1101,X` holds the object index — measured, §4b).

### Scene 2 (Crono's room), 20 objects

```
build/ct.exe --followvm --from <scratch>/s2_ctl.state --frames 1 | grep "^  f0 "
  obj0  ip=038D st=07 act=00 face=0 pos=(4080,4080)
  obj1  ip=0408 st=00 act=80 face=1 pos=(394,143)      <-- the player ($0197 = 2)
  obj2..obj7   st=80 act=00 pos=(4080,4080)
  obj8  ip=0669 st=04 act=00 face=3 pos=(368,226)
  obj9  ip=0760 st=80 act=00 face=3 pos=(392,79)
  obj10 ip=0761 st=07 act=80 face=0 pos=(392,79)       <-- THE CURTAIN
  obj11 ip=07F0 st=80 act=00 face=1 pos=(424,143)
  obj12..16    st=04 act=00
  obj17 st=80  obj18 st=04  obj19 st=03   (all act=00)
```
`st` = `$1100,X`, `act` = `$0F00,X`.
**`$1100,X == 7`: obj0 and obj10.** `$0F00,X != 0`: **only obj1 (the player) and obj10.**
So in scene 2 the ONLY object the probe can ever reach is the curtain, and it reaches it through
the `== 7` arm.

### Scene 439 (the fair), 32 objects, menu closed

```
build/ct.exe --followvm --from <scratch>/s439.state --frames 1 | grep "^  f0 "
  obj0  st=07 act=12 pos=(4080,4080)          <-- ==7, and act is 0x12, NOT 0x80 (see §4c)
  obj1  st=00 act=80 pos=(456,111)            <-- player
  obj2  st=07 act=00 pos=(4080,4080)          <-- ==7
  obj3  st=01 act=80 pos=(440,111)            <-- party follower
  obj4..8   st=80 act=00
  obj9  st=04 act=80 pos=(440,111)
  obj10..14 st=04 act=00 ;  obj15..17 st=80 act=00
  obj18 st=04 act=80 pos=(392,159)
  obj19..28 st=04 act=00 ;  obj29 st=80 act=00
```
**`$1100,X == 7`: obj0 and obj2.** `$0F00,X != 0`: obj0, obj1, obj3, obj9, obj18.

### What `$1100 == 7` *is*, from the per-frame object pass `C0/A810` (`JSR A810 from 0000D3`)

```
python tools/dis.py A810 0x80 1 1
  A810: LDA $7F2000 / ASL A / STA $BD ; A817: LDX #00 / A819: STX $6D
  A81B: LDA  $1100,X
  A81E: CMP  #03  / A820: BCC ->A870        ; <3  -> AAFD + A88D
  A822: CMP  #08  / A824: BEQ ->A83E        ; ==8 -> skipped entirely
  A826: CMP  #07  / A828: BEQ ->A86B        ; ==7 -> JSR C0/A947  (and NOTHING else)
  A82A: JSR C0/AA07 / A82D: JSR C0/A88D     ; otherwise -> tile clamp + the edge loader
```
`$1100 == 7` objects take a private path (`C0/A947`) that does the camera-window test and nothing
else — no mover, no clamp, no palette/edge load. That is the "static scenery actor" class, and the
curtain is one. **The `1D48` arm exists to test that class without the `CPX $97` self-skip.**

**NOT DERIVED**: the full enumeration of what each `$1100` value means (only the `A810` dispatch
above: `<3`, `==7`, `==8`, `bit7`, and "everything else"), and *why* the `==7` arm skips the self
check (no state on hand has the player object itself carrying `$1100 == 7`).

---

## 3. The `SBC`-without-`SEC` borrow — CONFIRMED as an effect, REFUTED as attributed

### 3a. The handler's first `SBC` really does consume the incoming carry — MEASURED

`--callrom` on the UP handler `C0/1D5E` from a state where `$01EB = 0x14` (obj10), `$01C5 = 0x005A`,
`$1894 (obj10 Y) = 0x04FF`. Expected result `objY - $C5 - (1-c)` = `0x04A5 - (1-c)`:

```
build/ct.exe --callrom C0/1D5E --from <scratch>/s2_probed2.state --regdp 0100 --regdb 00 --m8 --x8
  callrom: ret ops=8 a=04A4 x=0014 ... C=0 Z=0 N=0 V=0
build/ct.exe --callrom C0/1D5E --from <scratch>/s2_probed2.state --regdp 0100 --regdb 00 --m8 --x8 --carryin
  callrom: ret ops=8 a=04A5 x=0014 ... C=0 Z=0 N=0 V=0
```
`0x04A4` vs `0x04A5` — one apart, exactly the borrow. **The handlers are carry-sensitive. A clean
`SEC`-then-`SBC` port is wrong.**

### 3b. The carry that actually arrives comes from `CPX $97` (`1D36`), NOT `CMP #07` (`1D32`)

`1D36 CPX $97` sits **between** `CMP #07` and the `JSR`, and `CPX` sets the carry. Stepped
op-by-op with `--maxops` on `truce_market` (candidate obj11: `$1100 = 04`, index `X = 0x16 = 22`,
player `$0197 = 0x02`) — the two hypotheses disagree here (`4 >= 7` is false, `22 >= 2` is true):

```
for k in 22..30: build/ct.exe --callrom C0/1CFC --from saves/truce_market.state \
                   --rega 0000 --regdp 0100 --regdb 00 --m8 --x16 --maxops $k
  ops=22  a=2C04 x=0016 p=30 C=0 Z=0 N=0   <- LDA $1100,X  -> 0x04
  ops=24  a=2C04 x=0016 p=B0 C=0 Z=0 N=1   <- CMP #07      -> carry CLEAR
  ops=26  a=2C04 x=0016 p=31 C=1 Z=0 N=0   <- CPX $97      -> carry SET   (overwrites it)
  ops=28  a=2C04 x=0016 p=31 C=1           <- STX $EB
  ops=29  a=2C04 x=0000 p=33 C=1 Z=1       <- LDX $DB      ($01DB = 0 -> the UP handler)
  ops=30  a=2C04 x=0000 p=33 C=1 sp=06F9   <- JSR ($1D56,X)  ENTERED WITH C=1
```

And the `== 7` arm, stepped the same way on scene 2 (candidate obj10, `$1100 = 07`, `X = 0x14`):

```
for k in 65..73: build/ct.exe --callrom C0/1CFC --from <scratch>/s2_atwall.state \
                   --rega 0000 --regdp 0100 --regdb 00 --m8 --x16 --maxops $k
  ops=67  a=0607 x=0014 p=30 C=0 Z=0 N=0   <- LDA $1100,X -> 0x07
  ops=69  a=0607 x=0014 p=33 C=1 Z=1 N=0   <- CMP #07 -> C=1, Z=1
  ops=70  ... p=33 C=1 Z=1                 <- BEQ taken -> 1D48
  ops=73  ... sp=06F9 C=1                  <- JSR ($1D56,X)  ENTERED WITH C=1
```

**Canonical rule for the port:**

| arm | reached when | carry into the handler |
|---|---|---|
| `1D48` | `$1100,X == 7` | **always 1** (from `CMP #07`, equal) |
| `1D3A` | `$1100,X` bit7 clear and `!= 7` and `X != $0197` | **`X >= $0197`** (from `CPX $97`, both 8-bit) |

Since the scan runs top-down and `X == $0197` is skipped, the second row is `1` for every object
whose index is **above** the lead PC's and `0` for every object **below** it.

⚠ **disasm-only, unconfirmed:** the `X < $0197 → carry 0` half. I could not reach it on the oracle.
`$0197 == 2` (player = obj1) in every field state in `saves/` except `saves/solo_forest.state`
(`$0197 == 4`), and there obj0/obj1 both have `$0F00 == 0` so the scan never reaches them
(`build/ct.exe --read "7F2000+2,7E0197+2,7E0F00+16,7E1100+16" --from saves/solo_forest.state` →
`$0197 = 0400`, `$0F00 = 00FF 00FF 8001 00FF …` i.e. obj0=00 obj1=00 obj2=80). `obj0` is additionally
never testable at all: the loop `1D21 DEX / DEX / BNE ->1D1C` exits the moment `X` reaches 0.

### 3c. The **perpendicular** `SBC` always runs with carry CLEAR — MEASURED

Between the two `SBC`s sits `CMP #01C0 / BCS`; not taking the branch means C = 0. So the across-axis
delta is *always* `obj - player - 1`. Direct read-out, full probe, scene 2 at the wall
(player `$1802 = 0x18A4`, obj10 `$1814 = 0x1880`, difference `-0x24`):

```
build/ct.exe --callrom C0/1CFC --from <scratch>/s2_atwall.state --rega 0000 --regdp 0100 --regdb 00 --m8 --x16 --writes
  callrom: ret ops=94 a=0024 x=0014 y=0000 sp=06FD dp=0100 db=00 p=21 C=1 Z=0 N=0 V=0
  callrom: verdict SEC
  w 0001DB = 00 / 0001DC = 00   (by C0:1D03)
  w 0001EB = 80 / 0001EC = 00   (by C0:1D08)
  w 0001C3 = A4 / 0001C4 = 18   (by C0:1D0F)      ; player X 0x18A4
  w 0001C5 = 7F / 0001C6 = 06   (by C0:1D14)      ; player Y 0x067F
  w 0001EB = 14                 (by C0:1D4A)      ; <-- the 1D48 (==7) arm
```
`a = 0x0024 = 36`. Borrow model `~(0x1880 - 0x18A4 - 1) = 0x24`. Clean model would be `0x23 = 35`.
**36 measured → borrow confirmed.**

### 3d. The along-axis limit is **inclusive of exactly `0x1C0`** on the `==7` arm — MEASURED

Because the along-axis abs is `~(objY - playerY - 0)` = `playerY - objY - 1` when `c = 1`, an object
exactly `0x1C0` (= 28.0 px) away still passes. Walked to three Y values 1 px apart and called the
whole probe. obj10 `$1894 = 0x04FF`:

```
build/ct.exe --read "7E1802+2,7E1882+2,7E1602+1" --from <scratch>/s2_y{35,36,37}.state
  n=35 -> 7E1882 = 0x06DF   n=36 -> 0x06CF   n=37 -> 0x06BF   (7E1802 = 0x18A4, 7E1602 = 00 = UP)

build/ct.exe --callrom C0/1CFC --from <scratch>/s2_y35.state --rega 0000 --regdp 0100 --regdb 00 --m8 --x16
  callrom: ret ops=145 a=0180 x=0000 ... C=0   verdict CLC
build/ct.exe --callrom C0/1CFC --from <scratch>/s2_y36.state ...
  callrom: ret ops=145 a=0180 x=0000 ... C=0   verdict CLC
build/ct.exe --callrom C0/1CFC --from <scratch>/s2_y37.state ...
  callrom: ret ops=94  a=0024 x=0014 ... C=1   verdict SEC
```
`0x06BF - 0x04FF = 0x1C0` exactly → **ACCEPT**. A clean subtract would compute `0x1C0` and reject.
(The same is visible in real play: pressing A at `Y = 0x06BF` sets `$1C14 = 01`, §5.)
`a = 0x0180` on the exhausted scans is `1D25 LDA #80` with the stale high byte, not a distance.

---

## 4. `$0F00,X` at `C0/1D1C` **is** the camera-window latch this repo already ports

### 4a. Same byte — measured writer + the routine

```
build/ct.exe --monitor "...,7E0F14:1,..." --from <scratch>/s2_ctl.state --btnscript "10:60,0:6,100:4,0:60" --diff --writer
  f23 ... 7E0F14=80(C0:A983) ...        ; obj10's slot = $0F00 + 0x14
```
`C0:A983` is the post-PC of `A980 STA $0F00,X`, inside the window test that `A947` falls into:

```
python tools/dis.py A947 0x1C 1 1  +  python tools/dis.py A960 0x40 1 1
  A947: LDA $1D0A / SEC / SBC #04 / STA $C3
  A94F: LDA $1D0C / CLC / ADC #04 / STA $C4
  A957: LDA $1D0E /               STA $C5
  A95C: LDA $1D10 / CLC / ADC #05 / STA $C6
  A964: LDX  $6D
  A966: LDA  $1801,X / ASL A / CMP $C3 / BMI ->A985 / CMP $C4 / BPL ->A985
  A972: LDA  $1881,X / ASL A / CMP $C5 / BMI ->A985 / CMP $C6 / BPL ->A985
  A97E: LDA #80 / A980: STA $0F00,X / A983: SEC / A984: RTS
  A985: STZ $0F00,X / A988: CLC / A989: RTS
```
`$1D0A/$1D0C/$1D0E/$1D10` are the camera scroll words this repo already ports (memory
`ct-field-camera-scroll-model`). `JSR A810 from 0000D3` is the frame leg. **So yes: the probe's
liveness gate is the camera-window membership byte, at the same stride-2 index.**

The twin writer `C0/A88D` (for the non-`==7` classes, `JSR A88D from 00A82D / 00A873`) builds a
*slightly different* window — `A8B3 LDA $1D0E / SEC / SBC #02 / STA $C5` where `A947` uses `$1D0E`
raw — is **edge-gated** (`A8E5 LDA $0F00,X / BMI ->A90E` — already set, do nothing) and clears via
`A915 STZ $0F00,X`. It is additionally gated on `A894 LDA $1A81,X / BEQ ->A910 / BMI ->A910`.

Cross-check of that model against the measured tables (this is what makes the reading trustworthy):

```
build/ct.exe --read "7E1D0A+1,7E1D0C+1,7E1D0E+1,7E1D10+1" --from <scratch>/s2_ctl.state
  = 20 / 40 / 02 / 1E     ->  window ($C3..$C4, $C5..$C6) = (28..68, 2..35) for the ==7 class
  obj10: $1801=0x18 -> ASL 48  in [28,68);  $1881=0x04 -> ASL 8 in [2,35)   => $0F00 = 0x80  ✓ measured 0x80
  obj0 : pos (4080,4080)                                                     => STZ         ✓ measured 0x00
build/ct.exe --read "7E1A80+42" --from <scratch>/s2_ctl.state
  = 0001 0001 0001 0001 0001 0001 0001 0001 0000 0000 0001 0000 0000 ...
    -> $1A81,X != 0 only for obj0..obj7 and obj10   => A88D never even looks at obj11..obj19
                                                    => their $0F00 stays 0   ✓ measured 0x00
build/ct.exe --read "7E1A80+26" --from saves/truce_market.state
  = 0001 x8, then 0000 0000 0000 0001 0000   -> obj11 passes the gate  ✓ measured $0F16 = 0x80
```
Both scenes' `$0F00` tables reproduce exactly. **The probe can only ever see objects that are (a)
camera-window resident and (b) either `$1100 == 7` or carrying `$1A81,X != 0`.**

### 4b. Stride and neighbour bytes (needed by any port that keeps separate C arrays)

```
build/ct.exe --read "7E0F00+16,7E1100+16" --from <scratch>/s439.state
  7E0F00 = 12 FF 80 03 00 FF 80 00 00 FF 00 FF 00 FF 00 FF
  7E1100 = 07 00 00 00 07 00 01 01 80 02 80 03 80 04 80 05
```
Even bytes match `--followvm`'s `act`/`st` columns object for object → **stride 2**. The odd byte
`$1101,X` is the object index (`00 00 00 01 02 03 04 05`), and `$0F01,X` is written `#FF` by
`A900 LDA #FF / A902 STA $0F01,X`.

### 4c. ⚠ `$0F00,X` is **not** always `0x80`/`0x00` — it can be an aliased spill

Scene 439's obj0 reads `$0F00 = 0x12`, and the probe's test is `BNE`, so it passes:

```
build/ct.exe --monitor "7E0F00:1,7E0F04:1,7E0F02:1,7E0F12:1,7E0F24:1" --from <scratch>/s439.state --btnscript "0:40" --writer
  f0      7E0F00=12*(C0:A9C1)  7E0F04=00*(C0:A988)  7E0F02=80*  7E0F12=80*  7E0F24=80*
```
`C0:A9C1` is the post-PC of `A9BE STA $0E80,Y` (`python tools/dis.py A98A 0x44 1 1`) — `$0E80 + 0x80
== $0F00`. So an out-of-range `Y` in the `$0E80` linked-list code **spills into `$0F00`**, and the
probe's `BNE` then treats obj0 as live. Harmless in that state (obj0 sits at `(4080,4080)`, far
outside the box) but it is a real ROM behaviour that only survives a port over a **flat WRAM plane**;
a port with an isolated `objActive[64]` array cannot reproduce it. Flagged, not chased further —
**NOT DERIVED**: which `$0E81,X` produced `Y = 0x80`, and whether any scene ever spills a value that
matters.

---

## 5. GROUND TRUTH — scene 2, A at the curtain

State: `<scratch>/s2_ctl.state` (loc 2, `$011F = 1`, `$0134 = 0`, `$0197 = 0002`, 20 objects),
driven `--btnscript "10:60,0:6,100:4,0:60"` (walk UP into the wall, release, press A).

### Inputs at the press

| datum | address | value |
|---|---|---|
| facing (`$1600,X`, player) | `$7E1602` | `00` = **UP** → `$01DB = 0` → handler `C0/1D5E` |
| player X (`$1800,X`) | `$7E1802` | `0x18A4` (394.25 px) |
| player Y (`$1880,X`) | `$7E1882` | `0x067F` (103.9 px) |
| curtain X (obj10) | `$7E1814` | `0x1880` (392 px) |
| curtain Y | `$7E1894` | `0x04FF` (79.9 px) |
| curtain `$1100,X` | `$7E1114` | `0x07` → the `1D48` arm |
| curtain `$0F00,X` | `$7E0F14` | `0x80` (written `C0:A983`) |
| curtain `$1C00,X` (prio) | `$7E1C14` | `0x07` → `CMP #02 / BCC` passes |
| curtain `$1C01,X` | `$7E1C15` | `0x00` → `BNE ->5B1E` passes |
| curtain `$1000,X` | `$7E1014` | `0x09` → `BMI` not taken, passes |
| curtain IP `$1180,X` | `$7E1194` | `0x0761` (before) |
| `$01EB` at the top of the frame | `$7E01EB` | `0x80` (written `C0:8823`) → `1AC6 BPL` not taken → probe runs |

(`build/ct.exe --read "7E1C15+1,7E1C14+1,7E1194+2,7F2143+2,7E1A94+2,7E1015+1" --from <scratch>/s2_atwall.state`)

### The probe's verdict — measured

```
build/ct.exe --callrom C0/1CFC --from <scratch>/s2_atwall.state --rega 0000 --regdp 0100 --regdb 00 --m8 --x16 --writes
  callrom: ret ops=94 a=0024 x=0014 sp=06FD dp=0100 db=00 p=21 C=1 Z=0 N=0 V=0
  callrom: verdict SEC
  w 0001EB = 14  (by C0:1D4A)
```
**CARRY SET. `$01EB` ends up holding `0x14` — obj10 (index*2), latched by `1D48 STX $EB`, the
`$1100 == 7` arm.** Scan cost 94 ops: obj20 down to obj11 rejected at `1D1C` (`$0F00 == 0`, 5 ops
each), obj10 accepted.

### The same thing in real play (no callrom)

```
build/ct.exe --monitor "7E01EB:1,7E1194:2,7E1C14:1,7E1114:1,7E1014:1,7E0F14:1,7F0042:1,7E1882:2,7E1602:1,7E0134:2" \
  --from <scratch>/s2_ctl.state --btnscript "10:60,0:6,100:4,0:60" --diff --writer
  f40     7E01EB=80(C0:9941)  7E1194=6107(C0:5AC2)  7E1C14=07  7E1114=07  7E1014=09  7E0F14=80(C0:A983)  7E1882=7F06*
  f67     7E01EB=14*(C0:1D4A) 7E1194=7807*(C0:5AC2) 7E1C14=01*(C0:5B15) 7E1114=07  7E1014=09  7E0F14=80  7E1882=7F06
  f68     7E01EB=80*(C0:8823) 7E1194=7807          7E1C14=01
  f76 / f85 / f97   7E1194=7A07 / 8507 / 8D07  (the invoked script running)
  f113    7E1194=6107*(C0:5AC2)  7E1C14=07*(C0:5FA3)   (RETURN: IP and priority restored)
```
* `C0:1D4A` = `1D48 STX $EB` — the `==7` arm, in live play.
* `C0:5B15` = `5B12 STA $1C00,X` — **the invoke set priority to 1**, exactly `C0/5AC5`'s tail.
* `C0:5AC2` = `5ABF STA $1180,X`, the VM runner's normal IP writeback (so `--writer`'s "last writer
  per frame" hides the invoke's own `5B0B STA $1180,X`; the value it wrote is confirmed separately
  below).
* The press is at script frames 66-69 and lands on **f67** — one frame of joypad pipeline.

### The invoked function

`5AFA LDA $EB / ASL x4 / CLC / ADC #0002 / TAX / LDA $7F2001,X` → `$7F2001 + 0x14*16 + 2 = $7F2143`:

```
build/ct.exe --read "7F2141+16" --from <scratch>/s2_atwall.state
  = 5907 6207 A407 A507 A507 A507 A507 A507
    slot0=0x0759  slot1=0x0762  slot2=0x07A4  slot3..7=0x07A5
build/ct.exe --read "7F2762+24" --from <scratch>/s2_atwall.state
  = (24 bytes of the scene's event script, beginning with op B2 -- run the command on your own state to see them)
```
**fn slot 1 = IP `0x0762`**, matching `field_action_probe.md` §4's addressing
(`$7F2001 + obj*32 + slot*2`), and the live `$1194` at f67 (`0x0778`) is that script already 0x16
bytes in by the end of the frame. (`$7F0042` — the `glb[66]` byte the parent dossier attributes the
bit-6 toggle to — stayed `00` across the whole run; **NOT DERIVED** whether that attribution is right.)

---

## 6. What the port must reproduce (the short list)

1. Guard order at `C0/1AAC`: A-edge `$00F6` bit7 → **`$0134 == $FFFF` swallows and clears** →
   `$011F != 0` → `$01EB` bit7 clear means invoke without probing.
2. `$01EB = $80` written at the **top of every frame** by the player controller.
3. Scan **top-down** from `$7F2000 * 2`; `obj0` is unreachable; skip on `$0F00,X == 0` (a
   camera-window byte, **not** an "is scripted" predicate) and on `$1100,X` bit7.
4. Two arms: `$1100,X == 7` (no self-skip, carry in = 1) and everything else
   (`CPX $97` skips self, **carry in = `X >= $0197`**).
5. Both `SBC`s honour that carry; the **perpendicular one always borrows** (carry 0). The along
   limit is therefore reached at exactly `0x1C0` on a carry-1 entry and at `0x1BF` on a carry-0 one.
6. Invoke `C0/5AC5`: gates `$1C01,X == 0`, `$1100,X` bit7 clear, `$1000,X` bit7 clear,
   `$1C00,X >= 2`; then fn slot 1, priority 1, `STZ $1A80/$1A01/$1001`.

## 7. Everything I could NOT close

* **`X < $0197` → carry 0** — disasm-only, unconfirmed. No available state puts a window-live,
  non-bit7 object at an index below the lead PC's (`$0197` is `2` everywhere except
  `saves/solo_forest.state` where it is `4` but obj0/obj1 are dead).
* **The caller chain of the menu-exit block `C0/2166`-`21E0`**, the `$0215` exit codes, and the
  purpose of `21BB LDX $4216 / STX $34` (a non-`$FFFF` `$0134`, which does *not* block the probe).
* **The full meaning of `$1100,X`'s value space** — only `A810`'s dispatch (`<3`, `==7`, `==8`,
  bit7, else) is derived, and only `==7`/`==8`/bit7 are named.
* **Why the `1D48` arm skips the self check.** No state has the player carrying `$1100 == 7`.
* **The `$0E80,Y → $0F00` aliasing source** (`C0/A9BE`) — observed once (scene 439 obj0 = `0x12`),
  producer not traced.
* **`glb[66].bit6`** — the parent dossier's claim about the curtain's fn1; `$7F0042` did not change
  across the activation run. Not investigated further; not part of the four opens.
