# Op 0x8C places from the LOW bytes of two scene vars; op 0x21/0x22 fill them; the PC load writes $0197 (2026-09-23)

Found from the open item of battle_tech_damage_type6.md §5 ("the tech moves its victims, the port does not"). It
does not: the cartridge's `$1D0C`/`$1D23` never change from the Frog Squash battle's boot to its end
(`--monitor 7E1D0C:11,7E1D23:11` from saves/attract_07d_battle_f4540.state, one row over 620 frames). The victims
were **staged** wrong, because the 07D's field placed them wrong.

## 1. The staging reads the field objects

`CC/EA82-EA9B`: `$1D0C = 16 * $29BF + $29C1`, `$1D23 = 16 * $29C0 + $29C2` per enemy record. The records are
written by the field at the battle's entry (C0/0557-0616, writer C0:0589 at attract f4496):

```
C0/057C  LDA $1801,Y / SEC / SBC $F0 / STA $29BF,X      tile X - the arena origin (DP = $0100)
C0/0589  LDA $0A00,Y / AND #0F / STA $29C1,X            fine X = the object's screen X & 15
C0/0592  LDA $1881,Y / SEC / SBC $F1 / STA $29C0,X      tile Y
C0/059C  LDA $0A80,Y / AND #0F / STA $29C2,X            fine Y
```

Cartridge vs port (HEAD), slots 0-4 = obj 18 19 20 21 17: (33,183) (205,187) (80,127) (159,127) (120,127) against
(34,189) (205,186) (82,122) (157,121) (112,111).

## 2. The Sorcerer's `8C 08 09`

`ct.exe --vmdiff` from attract f4320 (saves/attract_07d_field_f4320.state) diverged first at f57: obj17 posX/posY
ours 0700/18FF, oracle 0780/19FF, on its `8C 08 09` (vars `$7F0210 = 07 00`, `$7F0212 = 19 00`).

`python tools/dis.py 04BC3 0x60 1 0`:

```
4BC3  INY / TYX / LDA $7F2001,X / REP #20 / AND #00FF / ASL / TAX / SEP #20
4BD2  LDA $7F0200,X            8-bit: the var's LOW byte
4BD6  LDX $6D / STA $1801,X    = the X TILE
4BDB  STA $D9 / INY ...        (no store to $1800 -- op 0x8B has `LDA #80 / STA $1800,X` here, 0x8C does not)
4BEC  LDA $7F0200,X / LDX $6D / STA $1881,X     the Y TILE
4BF7  LDA #FF / STA $1880,X
```

So X = tile * 256 + whatever `$1800` held (0x80 from the Sorcerer's own earlier `8B`), Y = tile * 256 + 0xFF.
The port's live VM (`applyEffect`) and its init pass (`runFunc`) read the var with `r16`, which in src/ct_vm.c is
BIG-endian (`r8(a) << 8 | r8(a + 1)`), i.e. `tile << 8`, and took `>> 4` as pixels: X at the tile's left edge,
Y = tile * 16 - 1/16 -- the foot of the tile ABOVE. The Sorcerer: 8 px left, 16 px high.

## 3. The vars come from op 0x21 / 0x22 (C0/65AA / C0/65EB)

```
65AA  INY / TYX / TDC / XBA / LDA $7F2001,X / TAX          0x21: X = the operand, an object OFFSET
65EB  ... LDA $7F2001,X / TAX / LDA $97,X / BRA 65B2       0x22: X = $0197 + operand (a party slot)
65B2  TAX / LDA $1801,X / STA $D9 / LDA $1881,X / STA $DB  the object's TILE bytes
65CE  LDA $D9 / STA $7F0200,X      8-bit store into the X var's LOW byte
65E3  LDA $DB / STA $7F0200,X      then the Y var's
```

The Hench and the Vamps are spawned the same way, on the Sorcerer's tile. And obj8 (sprite 133, loc 07D and six
more castle scenes) has `22 00 06 07 / 8C 06 07` in its fn0: it spawns on the lead's tile at the LOAD. Oracle,
attract f4289: `$7F020C = 07 00 21 00` by C0:65E7, `$1810 = 0007` by C0:4BDB, `$1890 = FF21` by C0:4BFC.

The init pass had no op 0x21 at all, and its 0x22 read `actors[op1 + 1]` -- the C record, not WRAM (for the lead
the record says "the arrival is the host's": 0). HEAD still got obj8 right by accident: the lead's own `8B` had
left `a->y = tile * 16 + 16` in the record, so `>> 4` gave tile + 1, and the big-endian `>> 4` in 0x8C took it back.

## 4. The PC load op writes the party table itself (C0/42B3 / 4327 / 43DA)

`42B3 STX $97` (16-bit: $0197 and $0198) in the slot-0 arm, `STX $99` / `STX $9B` in the other two, before
`JSR C0/448D`. The init runner walks the objects upwards from 0 (`C0/58EA LDX #0000`), so a later object's
`22 00` -- and a follower's `LDA $97` (C0/435C) -- sees the lead. The port wrote the table only after every fn0 had
run, so both read the boot's 0x80: obj8's vars came from the wrong place, and every follower took the no-leader
arm. Oracle, the scene-5 north door into 439 (saves/predoor_f2f_s5.state): `$0197 = 02` by C0:42B3 and
`$0199 = 06` by C0:4327 in the same load frame -- Marle (obj3) takes the LEAD COPY, not the arrival arm. The
bytes are the same there (the lead stands on the arrival tile).

## 5. The port (src/ct_vm.c; assets/script.py follows)

* `vmPos8C`: tile = `r8(LOC + var * 2)` for both axes, `$1801`/`$1881`/`$1880 = FF`, `$1800` kept, no zero guard;
  `applyEffect` and `runFunc` both call it.
* `runFunc` op 0x21/0x22: X = operand / `$0197 + operand`, the tile bytes from WRAM `$1801`/`$1881`.
* `pcSlotArmCommon`: `$0197/$0198` (or `$0199`/`$019B`) := the object offset, 16-bit, at the load op.
* assets/script.py mirrors all three in its static resolve (the packed actor table `ct.exe --vm` compares): the
  seven scenes whose sprite-133 object spawns on the lead now place it, on the static resolve's zero arrival.

## 6. Graded

* `attract-07d-enemy-staging-vs-oracle`: the five `$29BF` records, byte for byte the cartridge's.
* `vmdiff-07d-field-sorcerer-placement`: the lockstep's first divergence is f81 (was f57).
* `field-07d-obj8-spawns-on-lead-vs-oracle`: obj8 at `0007 / FF21`, `$0197 = 02`, as the cartridge's load.
* `attract-07d-frog-squash-five-numbers`: the damage numbers where the cartridge's OAM `$0700` has them at f5100:
  (64,112) (104,112) (143,112) (17,168) (189,172).
* The enemies' spread after spawning is the logical oracle's frame for frame (spawn (120,415) at oracle f4395 =
  port f6434, arrival (33,471) (205,475) (80,415) (159,415) at f4424 = f6463).

## 7. ~~Open~~ CLOSED 2026-09-23 (disasm/notes/field_d9_party_walk.md): the D9 walk was 8 frames short (exposed, not caused)

Oracle, real time and logical alike: the Sorcerer's `8C` to the Hench spawn is 40 frames; the port takes 32. Between
them is the party's op 0xD9 (C0/326C, MOVEPARTY): the ROM repoints the three PCs to the walk scripts it builds at
`$7F:3720/3740/3760` (IPs 171F/173F/175F -- the lockstep's first divergence, f81) and parks on `$7F03FC` until all
three are back at priority 0; the port drives the movers straight from the op and ends 8 frames early. HEAD's wrong
spawn point made the enemies' spread 5 frames longer and hid most of it; with the spread exact, the battle now
stages 8 frames early in the port's count and every later attract leg moved by -6 to -8 frames (checks re-pinned
at the shifted frames, same content).
