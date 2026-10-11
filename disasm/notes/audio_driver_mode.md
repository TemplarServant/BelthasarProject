# The driver MODE byte `$1E10` — why battles have no sound

Derived from the ROM + oracle. `dis.py <fileoffset>` (bank C0 = the offset directly); the field code
runs with **D = $0100**, so a direct-page `$FA` is WRAM **$01FA**. Bank $C7 (the sound driver) runs
with **D = $1E00**, so a direct-page `$10` there is **$7E:1E10** — the same byte.

`src/audio.c` already carries the consequence of this byte and says so:

```c
#define C7_SFXSET_A  0x74309u   // driver mode 0 (field)  -- IPL block 1
#define C7_SFXSET_B  0x75B0Bu   // driver mode 1 (battle) -- the block the IPL never uploads
```

`CtAudioLoadSong` already ports the `$C7:04C0` block that swaps the `$2F00` SFX set when the mode
changes. **Nothing in the engine ever writes `$1E10`**, so the mode is permanently 0, `SFXSET_B` is
never uploaded, and every battle SFX plays against the field sample set.

## 1. The three CPU-side writers (a ROM-wide scan for `STA $1E10`)

Only three, all in bank C0.

### 1.1 BATTLE — C0/1BAB (`dis.py 1BAB`)

```
1BAB  PHB / PHD / REP #20 / LDA #0100 / TCD / SEP #20 / PHA / PLB
1BB7  LDA $7E2A1F / BIT #40 / BNE ->1BD5     ; bit6 set -> SKIP the mode write entirely
1BBF  LDA #FF / STA $1E10                    ; MODE = $FF
1BC4  LDA $FA / STA $1E01                    ; the BATTLE SONG id, from $01FA
1BC9  LDA #14 / STA $1E00                    ; command $14
1BCE  JSL $C7:0004 / PLD / PLB / RTL
```

`$1E10` is written as `$FF`, not 1. The DRIVER normalises it: `C7:04D0` writes 1 (measured below),
which is exactly what `CtAudioLoadSong`'s ported `$C7:04C0` block does (`M8(m,0x1E10) ? 1 : 0`).

### 1.2 FIELD — C0/1BE6 (`dis.py 1BE8`)

```
1BF2  LDA $7F01EC / BEQ ->1C09               ; 0 -> the 1C09 arm
1BF8  DEC A / BEQ ->1BFE                     ; 1 -> test $7E2A1F bit5
1BFB  DEC A / BRA ->1C09                     ; 2 -> the 1C09 arm
1BFE  LDA $7E2A1F / BIT #20 / BNE ->1C09 / BRL ->1C90
1C09  LDA $7E2A1F / BIT #40 / BNE ->1C7F     ; the same bit6 suppression
1C11  LDA #00 / STA $1E10                    ; MODE = 0
1C16  $1E01=00 / $1E02=00 / $1E03=FF / $1E00=81
1C90  (the other arm) LDA #00 / STA $1E10 ; $1E01=00 / $1E02=00 / $1E03=FF / $1E00=81
```

Both field arms set the mode to 0 and issue a raw `$81`. `$7F01EC` selects the arm; NOT derived.

### 1.3 The suppression gate `$7E2A1F`

Written at exactly one site, `C0/3A9E`, from a SCRIPT operand (`LDA $7F2001,X / STA $7E2A1F`, in the
changeloc staging next to `$7E2989`). So it is a per-location byte the scene supplies; **bit6
suppresses the mode write on both paths** and bit5 selects between the two field arms.

## 2. Oracle — the whole thing measured on a real battle entry

```
ct_probe <rom> --monitor "1E10:1,1E00:4,2A1F:1,01FA:1" \
   --from saves/battle_re/forest_wait_ambush_active.state --btnscript "0:400" --writer --diff
```

```
f0     $1E10=00   $01FA=45   $2A1F=00
f82    $1E00 = 00 9B 80 FF                 ; SFX $9B  (consumed; writer C7:0196)
f115   $1E00 = 00 84 80 FF                 ; SFX $84
f284   $1E10 = FF (writer C0:1BC4)         ; THE ARM -- the STA at 1BC1
       $1E00 = 14 45 B0 04                 ; cmd $14, song $45 = $01FA
f320   $1E10 = 01 (writer C7:04D0)         ; the DRIVER normalises $FF -> 1
f331   $1E00 = 00 45 B0 04                 ; consumed
```

`$01FA = $45` is seeded at boot by `C0/0C58` (`LDA #45 / STA $FA`, alongside `$01FB = $65` and
`$01FC = $53`).

**Our engine stages the same two SFX** — a `--scene 19 --battletest` run with `CT_SNDTRACE=1` posts
`cmd=18 p1=9B` and `cmd=18 p1=84`, matching f82/f115 above. The battle SFX staging is already right.
What is missing is only the f284 arm.

## 3. What the port is

At the point the engine enters battle, do what C0/1BAB does:

```
if (!($7E2A1F & 0x40)) {          // the per-location suppression
  mem[0x1E10] = 0xFF;             // 1BBF
  mem[0x1E01] = mem[0x01FA];      // 1BC4 -- the battle song id
  mem[0x1E00] = 0x14;             // 1BC9
  CtAudioService(...);            // 1BCE JSL $C7:0004
}
```

and the mirror (mode 0 + cmd `$81`) on the way back to the field. `CtAudioService` already routes
`$14` into `CtAudioLoadSong` (`cmd == 0x10 || 0x11 || 0x14 || 0x15`), and `CtAudioLoadSong` already
performs the `$2F00` block swap that uploads `C7_SFXSET_B` once the mode flips. No new audio code.

`$1E02`/`$1E03` are NOT written by C0/1BAB — at f284 they read `B0 04`, left by an earlier command.
That is the documented stickiness of the mailbox tail; a port must not zero them.

## 4. Open — and why the port is not attempted in this note

- **The TRIGGER is not derived.** No static `JSR`/`JSL`/`JMP` to C0/1BAB exists anywhere in the 4 MB
  image, so the call is indirect. The oracle pins WHEN it happens (f284 of the run above) but not
  WHO calls it. Hooking it at whatever the C engine happens to call "battle entry" is exactly the
  kind of stand-in the mandate forbids, and the last change shipped on a partial derivation cost
  three user-visible regressions.
- `$7F01EC` (the field-arm selector) is not derived.
- Whether `$7E2A1F` bit6 is ever set in practice is not measured (it was 0 throughout the run above).
