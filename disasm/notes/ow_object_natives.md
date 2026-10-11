# Overworld object natives (op 0x34 targets) -- the ones ported, one entry each

Op 0x34 (C2/1807 `JMP ($005B)`) runs whatever bank-C2 routine the object's script names. The port's table is
world.c `obNative`. An address it does not have is printed once (`owobj: op 34 -> C2/xxxx is an UNDERIVED native`)
and skipped. The older entries (78A1, 7575, 754D, 7598, 786F) are documented in place; see ow_objvm.md for the VM.

## C2/79E0 -- the Lavos fragments' random spread (2026-09-19)

USER REPORT (translated from Portuguese) "in the scene with lavos, the smaller earth fragments are all at one single point vertically". The attract's
1F7, Lavos's eruption: each small fragment's script calls `34 E0 79`.

```
79E0  JSR C2/2336                  A = C0:FE00[$1B30], INC $1B30 (8-bit index)
79E3  STA $4202 / LDA #30 / STA $4203 / NOP x4
79EF  LDA $4216 / BMI -> LDA $4217 / INC A     the product's high byte, rounded on the low byte's bit 7
79F4  LDA $4217
79FD  REP #20 / LDX $4E / AND #00FF / CLC / ADC $0014,X / STA $0014,X     X += 0..48
```

`--hookpc C279E0` visits it at f2984, f2985 and f3000. The cartridge's fragments sit at X 657..693. The port's all
sat at their birth X 648, and `$1B30` did not advance for the natives after them. Check:
`attract-1f7-lavos-fragments-vs-oracle`. Cartridge f3120 against port f5152 now matches on every pixel,
57344 / 57344; before it was 56880. The 1999 A.D. colour-math frame also moved: 56350 -> 56448.

## C2/48CD -- the Epoch's leap (2026-09-19)

USER REPORT (translated from Portuguese) "in the final scene (overworld 1000ad), the sprites of the epoch-departing animation are broken". The attract's
1F0: the Epoch's script (obj 6) runs its flight loop `$7F:1492` (`31 02` move, `09 9C 14 7F` a trail child, `1B 22 FA`
count down) three times from `$7F:1416`: `0D 22 0E / 36 92 14 / 34 CD 48 / 0D 22 24 / 36 92 14 / 04 .. / 0D 22 0C /
36 92 14`. Between the first two rounds, C2/48CD:

```
48CD  REP #20 / LDX $4E / LDA $001C,X / BMI -> LDA #FE00 : LDA #0200 / CLC / ADC $0014,X / STA $0014,X / JMP C2/1CB8
```

That is a leap of 0x200 px along the X velocity, then the world wrap. Cartridge `$0CC4`: 02A0 at f8748 -> 04A0 -> 04A8 at
f8749. The Epoch comes round the 0x600 torus to 0178 by f8840, over Truce. The port skipped it, and its Epoch was at 04B8,
off screen. Check `attract-1f0-epoch-return-vs-oracle`.

## Anim op 6 (C2/0F00) and the NMI's VRAM queue (C2/0405) -- not a native, the same report

The Epoch's anim (`$1E` -> C3:E582) starts with op 6. It is not a particle spawner (ct-ow-anim-subvm said so, wrongly).
It is the VRAM upload queue:

```
0F00  SEP #30 / LDX $5F / INC $5E / [$52],1 -> $62,X (bank) / #80 -> $67,X (VMAIN) / REP #20 / [$52],2 -> $60,X (source)
      / [$52],4 -> $63,X (VRAM word) / [$52],6 -> $65,X (bytes) / $5F += 8 / STZ $5E / LDA #0008
0405  (NMI) $5E set -> RTS; $5F = 0 -> RTS; per entry: DMA ch 0 mode 01 -> $2118, $2116 = word, $2115 = VMAIN; STZ $5F
```

Its source is the sheet the loader's last step puts at `$7E:C800`: C2/2D6D `JMP C2/27BC`, `$C6:FDF3` = `$C5:B114`
through C3/0002. The Epoch's own entry is `$7E:C940` -> VRAM word `$1800`. The port parked on op 6 and never loaded the
sheet. So the Epoch was invisible, and its trail (script `$7F:149C`, anim `$35` -> E66D) drew names `$180..` out of stale
VRAM, which were the "broken sprites". Check `attract-1f0-epoch-departure-vs-oracle`: cartridge f8732 against port f10696,
57344 / 57344. (CtWorldNmiVramQueue in world.c; the port's tilemap streaming keeps its own queue.)
