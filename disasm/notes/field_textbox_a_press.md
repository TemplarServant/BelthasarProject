# The A press and the message box: `$0134` is the box's WAIT latch

Derived 2026-08-29 from the ROM + the oracle only. User report (translated from Portuguese): *"often when smashing A
quickly, the dialogue keeps repeating"*.

Every claim below cites `python tools/dis.py <fileoffset>` (bank C0 = the file offset directly) or a
`build/ct.exe --monitor` measurement. The field engine runs with **D = $0100**, so a direct-page
`$29`/`$34` in the disasm is WRAM **`$0129`**/**`$0134`**.

## 1. The frame order -- C0/0097

```
0097  LDA $00F0 / TSB $51 ; 009C  LDA $00F6 / TSB $50     ; the held / EDGE button words
00A1  JSR C0/18D9 ; 00A4 JSR C0/0C76 ; 00A7 JSR C0/881E
00AA  JSR C0/1AAC        ; THE A-BUTTON HANDLER  <-- first
00AD  JSL C0/1F87        ; THE MESSAGE BOX MACHINE <-- second
00B1  JSR C0/21E1 ; 00B4 JSR C0/274D ; 00B7 JSR C0/00BF ; 00BA JSR C0/EC60 ; 00BD BRA ->0097
```

The A handler runs **unconditionally, every frame, before the box machine**. There is no
"a box is open" gate anywhere in that chain.

## 2. The handler -- C0/1AAC (`dis.py 1AAC 0x34`, A 8-bit / X 16-bit)

```
1AAC  LDA $00F6 / BIT #80 / BNE ->1AB4 / RTS   ; the A EDGE ($00F6, not the held word)
1AB4  LDX $34 / BEQ ->1AC1                     ; $0134 == 0     -> fall through
1AB8  CPX #FFFF / BNE ->1AC1                   ; a countdown    -> fall through
1ABD  INX / STX $34 / RTS                      ; $0134 == FFFF  -> $0134 = 0 AND RETURN
1AC1  LDA $1F / BNE ->1AC6 / RTS               ; $011F = player control
1AC6  LDA $EB / BPL ->1AD5                     ; a latched target ($01EB) -> invoke it directly
1ACA  LDX $97 / LDA $1600,X / ASL A / JSR C0/1CFC   ; else PROBE  (-> $01EB, carry)
1AD3  BCC ->1ADC / SEP #30 / JSR C0/5AC5       ; THE INVOKE
1ADC  BRL ->1DF4                               ; ...and always the TREASURE probe
```

**`$0134` is the only thing that can spend an A press on the box, and it costs the press
entirely** -- the handler `RTS`es before `$011F`, before the probe, before the chest probe.

## 3. What `$0134` is -- the box machine C0/1F87

Dispatch (`dis.py 1F87 0x50`): `$0129` = 1 -> C0/1FB4, 2/3/4 -> 1FC0, **5 -> 1FCD**, **6 -> 2018**,
**7 -> 201C**, 8 -> 1FB4, 9/A/B -> 1FC0, C -> 1FC5, **D -> 2032**.

```
2018  JSR C0/2126 / RTL                 ; STATE 6 = THE TYPEWRITER. That is the whole handler:
                                        ; it reads no button, and nothing here touches $0134.
201C  LDA $62 / BEQ ->2056              ; STATE 7 ... 2056 BRA ->20A5
20A5  LDX $34 / BEQ ->2058              ;   $0134 == 0    -> ADVANCE NOW
20A9  CPX #FFFF / BEQ ->20B1            ;   $0134 == FFFF -> wait forever (RTL)
20AE  DEX / STX $34 / RTL               ;   else count the auto-advance down
2058  LDA $0215 ...                     ; the advance: code 0 -> $29 = 0D (CLOSE, C0/2081)
                                        ;   9/A/B/C -> $29 = 8 (page)   5/6/7/8 -> 8 if $0131 == 3
                                        ;   else 6 (keep revealing)     3 -> $0216 ? 6 : close
2032  LDA $27 / [DEC $27 / BEQ] x4      ; STATE D = the CLOSE ramp -> 204D STZ $29 / STZ $26
```

`$0134 = FFFF` is written beside `$0129 = 7` (C0/21C2, C0/21D3); when `$0216` is non-zero C0/21A8
loads `$0134 = $0216 * 15` instead -- the auto-advance countdown 20A5 decrements.
**So state 7 -- and only state 7 -- reads the button, through `$0134`.**

> **CORRECTION (2026-08-29, same day).** This paragraph first called `$0216` the Config "message
> speed" setting. It is not a setting at all: it is the **one-byte operand of an in-string control
> code**, read straight out of the text stream by the C2 text engine --
> `25945 LDA [$31] / 25947 STA $16 / 2594B INC $31` (`dis.py 0x25940 0x30 1 0`), which runs with
> D = `$0200`, so `STA $16` IS `$0216` and `INC $31` steps the string pointer past the byte it just
> consumed. A ROM-wide scan for an absolute writer (`8D/9C/8E/8C 16 02`) finds exactly one site, in
> bank `$E2`, and nothing in the field or menu banks -- there is no config path to this byte. Its
> partner is `$0215`, the control CODE itself: C0/2058's dispatch reaches the `$0216` arm only
> through `207D CMP #03 / BEQ ->2096`, i.e. code **3** = "pause here", operand = how long. So the
> auto-advance is a property of the STRING, not of the player's options, and the measurement below
> reading `$0216 = 00` means "this string carries no pause code", not "the setting is off". The
> mechanism above (C0/21A8's `$0216 * 15`, C0/2096's `$0216 ? keep revealing : close`) was read off
> the bytes and is unchanged -- only the NAME was wrong, and a wrong name is how a datum gets used
> for the wrong thing later.

## 4. MEASURED (the whole point)

`build/ct.exe --teleport 8 7 6 1 --from saves/truce_market.state --out build/s8t.state` puts the
lead at (120,111) facing DOWN in front of Taban (obj10) at the fair -- the same tile the port's
`Belthasar --scene 8` starts on. Then
`--monitor "7E0126:1,7E0129:1,7E0134:2,7E01EB:2" --from build/s8t.state --diff`:

| drive | result |
|---|---|
| `--btnscript "0:300"` (no input at all) | `$0129` 06 -> **07 at f35**, `$0134` 0 -> **FFFF** the same frame, then frozen |
| `--btnscript` A every 3 frames, 300f | `$0129` 06 -> **07 at f35 TOO**; 0D at f37; 00 at f48; new box 01 at f80; ... **3 boxes in 300 frames** |

and during the box, on **every** press frame, `$01EB` = `1400` (= obj10) and back to `8000` the next
frame: the press really did fall through to the probe (C0/1CFC latched Taban), and C0/5AC5 declined
it -- `LDA $1C00,X / CMP #02 / BCC ->5B1E`, the object is at priority 1 running the very script that
owns the box. `$011F` stays `01` throughout: the box takes no control lock, so the probe genuinely
runs. `$0216 = 00` -- this string carries no pause code (see the correction above), so
no auto-advance is in play and state 7 waits on the player.

**A press does NOT skip the typewriter. The reveal takes exactly as long with a 3-frame mash as
with no input at all.**

## 5. The port's divergence (fixed here)

`src/field.c`'s A dispatch keyed on `tbActive` ("a box is on screen") and spent an unfinished
reveal on `if (tbReveal < tbLimit) tbReveal = tbLimit;  // 1st press: finish the reveal` -- three
sites (options page, choice prompt, plain text). That is an invention: two fast presses blew a whole
box away (finish, then dismiss), the script returned, and the next press re-invoked the same NPC.
Same tile, same 3-frame drive, 300 frames: **the ROM opens 3 boxes, the port opened 12**
(`CT_TBTRACE=1 ... --scene 8 --walkscript "a:3"x100` -> `boxes=12`; slow drive -> `boxes=2`).

The fix is the ROM's own discriminator, not a debounce and not a scene gate: the press belongs to
the box **iff the box is in its wait state** (`$0129 == 7` / `$0134 == FFFF`), whose host equivalent
is `tbActive && tbAnim >= 40 && !tbScroll && tbReveal >= tbLimit`. Anything else falls through to
the probe, exactly as C0/1AAC does.
