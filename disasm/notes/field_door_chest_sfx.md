# The door and chest sounds — and the two routines they are NOT

Two of the most frequently heard field sounds were absent from the port, which transcribed the
instructions on either side of their `JSR` and skipped the call. This note records the derivation,
and corrects an attribution that a backlog sweep got wrong.

---

## 1. THE POSTER — C0/1B90, and its one-instruction sibling C0/1BA7

```
1B90  LDA $FC            ; the sound id -- $01FC (DP = $0100)
1B92  STA $1E01
1B95  LDY $97            ; Y = $0197 = the LEAD PC's object OFFSET (the party-slot table)
1B97  LDA $0A00,Y        ; that object's screen X LOW byte -> the PAN
1B9A  STA $1E02
1B9D  LDA #19
1B9F  STA $1E00          ; the COMMAND byte, written LAST
1BA2  JSL $C7:0004
1BA6  RTS

1BA7  LDA $FB            ; the same routine with a different id -- $01FB
1BA9  BRA ->1B92
```

**It is not the menu path.** `C2/EAF9` clears the throttle `$0D76` first and `C2/EB0E` can swallow a
post; `C0/1B90` touches neither, so it is unthrottled and must not be routed through
`fieldMenuSfx*`.

**The two ids are live bytes, not constants.** Cold init seeds them at `C0/0C50-0C5A`
(`ct_vm.c memBoot`): `$01FC = 0x53` (door), `$01FB = 0x65` (chest), `$01FA = 0x45`. Read back from
four unrelated oracle states (`truce_market`, `base_loc6`, `fresh439`, `com_jogo_salvo`) all four
report `$01FA..$01FC = 45 65 53`, so the seed is what the running game carries. The port reads the
addresses rather than baking the values, exactly as the ROM does.

`$0A00,X` is already maintained here: `CtObjScreenPos` (move.c, the graded port of C0/AB45) writes
`$0A00 + idx` every frame, and a live `--dumpwram` of scene 2 shows `$0A02 = 0x78` with `$0197 = 02`.

---

## 2. WHO CALLS THEM — and the correction

A byte scan of bank C0 for `JSR C0/1B90` / `JSR C0/1BA7` gives every caller in the bank:

| routine | callers |
|---|---|
| `C0/1B90` (id `$01FC`) | **C0/0D92, 0E8F, 1040, 1212, 149D** — the FIVE door handlers |
| `C0/1BA7` (id `$01FB`) | **C0/16EB** — the chest arm, and nothing else |

⚠ **The sweep that raised this item said the door sound is posted at `C0/0D49`. It is not.** The
`JSR` at `0D49` is `C0/28C0`, and that routine is not a sound at all — decoded with M=16 it is:

```
28C0 REP #20 / LDX #0000
28C4 LDA $7F1CC8,X   / BMI ->28D9   ; empty entry -> append
28CA CMP $5B         / BEQ ->28D6   ; already listed -> done
28CF INX / INX / CPX #0020 / BCC -> loop
28D6 SEP #20 / RTS
28D9 LDA $5B / STA $7F1CC8,X / BRA ->28D6
```

— an **append-if-absent into the 16-entry opened-door list at `$7F:1CC8`**. The port does not run
that either; it is a separate, still-open gap, listed in §5.

### The two conditionalities are OPPOSITE, and that is the interesting part

* **Door**: the `JSR` is straight-line INSIDE each handler, after the metatile bump:
  `LDA $7E3000,X / INC A / STA $7E3000,X / JSR C0/1B90 / LDA #imm / STA $60` (verified byte-for-byte
  at C0/1035 and C0/1494). So it fires **iff a handler matched** — which is exactly what the port's
  `CtFieldSceneDoorOpen` returns.
* **Chest**: `16EB JSR C0/1BA7` sits between `16E9 TRB $17` and `16EE LDX $5D`, i.e. **before** the
  `$FE`/`$E0` metatile test. So it fires on the **request itself**, whether or not the tile turns
  out to be a chest lid.

Measured on the port after wiring: the scene-4 drive raises the door request **ten** times (ten
`[door] tile(10,4)` lines, `opened=1` then nine `opened=0`) and posts **one** sound. The scene-26
drive that presses A three times on one chest posts **one** (the treasure probe declines the later
presses, so the request is never re-raised), and a chest already open at load posts **zero**.

---

## 3. HOW IT IS GRADED

Folded into the two checks that already prove the open itself, so the sound cannot drift apart from
the event that causes it:

* `field-scene4-door-opens-on-step` → `fieldsfx: door -> cmd=19 id=53 pan=A8 posted (C0/1B90, unthrottled)`
* `field-chest-opens-item` → `fieldsfx: chest -> cmd=19 id=65 pan=88 posted (C0/1B90, unthrottled)`

Mutations, both killed:

| mutation | result |
|---|---|
| pan `$0A00 + $0197` → constant `0x80` | BOTH checks red |
| the door post removed | door check red, chest check still green (right check, right blame) |

⚠ **This grades STAGING, not DSP output**, and that limit is measured rather than assumed: these
checks run with `SDL_AUDIODRIVER=dummy`, where `SDL_OpenAudioDevice` never opens and `CtAudio` is
never created — with `CT_AUDIO_FORCE=1` and `CT_AUDIO_VOICETRACE=1`, `CtAudioVoicesSounding` reads 0
for all 90 frames of a scene-2 run, i.e. for the scene's own MUSIC as well. Nothing about the two
new posts is special there; output grading needs an environment with a real audio device.

---

## 4. THE PROBE

`CT_FIELDSFX` — one line per field SFX post: `fieldsfx: <what> -> cmd=19 id=%02X pan=%02X posted`.
Distinct from `CT_MENUSFX`, which traces the throttled `C2/EAF9`/`C2/EB03` menu path.

---

## 5. OPEN

* **`$7F:1CC8`, the opened-door list** (C0/28C0, called at C0/0D49) is still unported — but ⚠ **the
  sentence that used to be here was wrong about WHY it matters, and the correction is worth more
  than the item.** It said the list is what "survives a re-bake or a location reload". It is not:

  ```
  C0/28AA  REP #20 / LDA #8080 / LDX #0000 / STA $7F1CC8,X / INX / INX / CPX #0020 / BCC   ; CLEAR
  ```

  and a byte scan of bank C0 gives that routine exactly **one** caller — `C0/0091`, inside the
  loader sequence at C0/0085 (`JSR C0/0B86 / 00F4 / B192 / 56A6` then the clear). So a LOCATION
  LOAD wipes the list, and open doors do not survive a location change in the ROM either.

  What the list is actually for is the **REPLAY**, `C0/28E1` (`LDA $7F1CC8,X / BMI exit /
  JSR C0/28F9`, and 28F9 re-applies the bump to `$7E:3000,X`), whose callers are `C0/18C4` and
  `C0/286C`. `C0/18C4` sits immediately after `C0/18B9` — the BATTLE-RETURN ack (`LDA #01 /
  TRB $17 / TSB $18`) that this port already transcribes at `src/field.c:5872`. So the list exists
  so that doors stay open across a battle return, which re-loads the L1 plane without changing
  location.

  ⚠ Whether the PORT needs it is therefore an empirical question about the port's battle return,
  and it is NOT established here: if the port's battle return leaves the live `CtScene`'s L1
  metatiles alone, its persistent one-shot INC is accidentally right; if it re-loads them, the
  doors are lost. Measure that before implementing anything.
* Whether `$01FB`/`$01FC` are ever REWRITTEN after cold init is unestablished. Four states all carry
  the seed, and the port reads them live either way, so a rewrite would be followed correctly — but
  no site that performs one has been looked for.
