# The field's sprite depth is the ROM's draw list, not a Y sort (2026-09-29)

Found by the attract audit after 6b8f2eb (item 4): in 0E6, c7683-7690 against p7585-7592, about 420 pixels of Robo each frame.
obj6 (Robo, the party's second member, $0199 = 0C) and obj11 (a robot) both have `$1880 = 24FF`. The cartridge draws
obj6 in front, and the port drew obj11 in front.

## 1. The ROM

The OAM build walks the draw list; nothing sorts by Y.

```
C0/B2A5  LDY #007E
B2A8     LDA $0E00,Y / BMI ->B2C0        the bucket's head ($80 = empty)
B2AD     STA $6D / JSR C0/B309           emit it
B2B2     LDX $6D / LDA $0E81,X / BMI ->B2C0 / STA $6D / JSR C0/B309 / BRA ->B2B2   the chain
B2C0     DEY / DEY / BPL ->B2A8          the next bucket, DOWN to 0
```

- **Placement in OAM.** C0/B309 -> C0/B334 streams each object into its priority group's OAM run (`$0F80 & 0C`: C at slot 4,
  default at 28, B at 104; C0/B276).
- **Front and back.** Inside a group, the object walked first takes the lower slot, and on the SNES the lower slot is in
  front (ct-sprite-unit-order).
- **The bucket** comes from C0/A9D6: `LDA $0A80,X / LSR / AND #FE / TAY`. That is a band of four scanlines of the screen Y.
- **Order inside a band** is the chain's order:
  - C0/A9CD links at the HEAD (A9F2-AA03: the old head becomes the next object).
  - C0/A810 relinks every live object in index order (`A817 LDX #00` ... `A840 INX / INX / CPX $BD / BMI`; the four legs
    A832 C98A / A835 A98A / A838 AB45 / A83B A9CD).
  - After that loop comes the party tail: `$0199` (A846-A855), then `$0197` (A858-A867).
  - So inside a band the object relinked last is in front, even when its exact Y is smaller than the others'.

**Measured on the rule's cartridge** (build/_divergence/after_6b8f2eb/fr, the audit's chained states):

- At c7686, c7687 and c7688 the walk is: bucket 5E = obj13, obj10, obj5; bucket 4E = obj6, obj14, obj11; bucket 3E = obj3,
  obj15, obj12.
- At c7687 the OAM has x 5C/4C (obj6) at #40-43, x CD (obj14) at #44-47, and x 59/49 (obj11) at #48-51.
- obj6 is in front because the party tail relinked it after the loop.

## 2. The port

The VM keeps `$0E00/$0E80/$0E81/$0E01` byte for byte while the frame legs run:

- ct_vm.c runs C0/A810's legs and the party tail.
- The two routines are move.c CtObjListUnlink / CtObjListLink.
- The port's WRAM at p7589 walks the same nine objects in the same order as the cartridge's at c7687.

The renderer (field.c, the compose block) sorted by (group, the actor's Y), stable by index. So two objects on one `$1880`
were drawn in index order, and a band's chain order was never consulted.

Now the depth key is the list's own walk: the first object walked is drawn last. The group stays the primary key.

**Port-side guard, not the ROM.** A frame keeps the Y sort when either of these holds (`CT_DRAWLISTDBG=1` names the object
and its `$0E01/$0F00/$1100`):

- an object the port draws is not in the list;
- the chain is malformed.

## 3. Result

- 0E6, c7683-7690 against p7585-7592: every frame 57344/57344 (c7687 was 56923).
- **The whole attract, the list against the Y sort.** Both are run under CT_AUDIO_LOCKSTEP=1; without it two identical runs
  already differ, because the real-time audio moves the timing.
  - The two orders differ on 73 of 11001 frames: 0DB, 0A5, 07D and 0E6.
  - Each frame was compared with the best cartridge thumbnail within the leg's offset (the audit's rule cache).
  - The list is closer on 71 frames, ties on 2 and is never farther.
  - The summed distance falls from 2486 pixels (Y sort) to 28 (the list).
- In the attract the guard is met in three stretches, always by the host's LEAD with `$0F00 = 00`, off screen:
  - 01B f3217-3313, at screen y 345;
  - 0A5 f5855-6293, at y 423;
  - 0E6 f7759-7875, at x -48.

## 4. What meets the guard (measured 2026-09-29, open)

- **The host's lead with `$0F00` clear.** The port draws the lead by its own predicate (CtSceneActorHidden), not by the
  latch. The ROM does not draw it: C0/A921 unlinked it, and C0/B309 never meets it.
- **Class-7 triggers.** The port's `placed` is `$0F00 != 0`, and C0/A947 (the class-7 leg, A86B) stores a non-zero latch
  (08 for p3field's obj0). But A947 never links: no A9CD, and C0/B0E6 skips class 7 (B0F8). So the ROM never draws one.
  - The port draws p3field's obj0, and the pinned one-pixel residual of field-bg-seed-party3-vs-oracle is that sprite.
  - field-bg-seed-tent-keeper-vs-oracle's two frames are the same kind.

**TRIED AND REVERTED: putting such an object behind the list instead of falling back.**

- It closed both bg-seed residuals: 91 -> 0 frames and 2 -> 0 frames.
- It worsened menu-resume-fade-in-vs-oracle's walk: 187 -> 366 px on the fade frames, 185 -> 0 on the lit one.
- Hiding obj0 outright gives the same 366, so obj0 is not the cause. The list is, because the menu's reload does not
  rebuild it:
  - C0/286C runs `B204` (`$0E00-$0EFF` = 80), `A7E9` (`$0F00` = 00 / `$0F01` = FF), then `B0E6`.
  - B0E6 walks the objects in index order: B127's window test and stores, AB45, A9CD, the CAD9 drain loop, B1B2, EA42.
    It relinks every visible object with no party tail, so the lead lands behind the later objects of its band.
  - The port keeps the list from before the menu, with the lead at its band's head, until the object pass relinks.
- The port's B0E6 (ct_vm.c palSweepB0E6) is partial: only B127's latch and the palette. Its note says the
  draw-list stores were deliberately left out.

**What closes it:** the whole C0/286C object re-init at the reload, with B0E6 and B127's six stores. Then the lead and the
class-7 triggers can follow the ROM's predicate (in the list or not drawn), and the guard goes.
